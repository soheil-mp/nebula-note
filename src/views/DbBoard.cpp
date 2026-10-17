// The board layout: a column per group of any groupable property, optional
// sub-groups as swimlanes, folded and hidden columns, and cards that drag
// between groups.
#include "DbViews.h"
#include "DbViewsPrivate.h"

#include "PropertyEditors.h"
#include "core/Workspace.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

using namespace DbViewsPrivate;

namespace
{
constexpr double kGap = 14;
constexpr double kHeaderH = 38;
constexpr double kLaneH = 36;
constexpr double kFoldedW = 44;
} // namespace

BoardView::BoardView(QWidget *parent)
    : DbView(parent)
{
}

void BoardView::setContext(const ViewContext &ctx)
{
    DbView::setContext(ctx);
    layoutBoard();
    update();
}

double BoardView::columnWidth() const
{
    const QString size = m_ctx.view.cardSize;
    return size == QLatin1String("small") ? 220 : size == QLatin1String("large") ? 320 : 264;
}

int BoardView::contentWidth(int available) const
{
    if (!m_groupProp)
        return available;
    return qMax(available, int(m_width + 20));
}

double BoardView::cardHeight(const RowRef &r) const
{
    const double colW = columnWidth();
    double h = 12;
    const QString preview = m_ctx.view.cardPreview;
    const double coverH = m_ctx.view.cardSize == QLatin1String("small") ? 90 : m_ctx.view.cardSize == QLatin1String("large") ? 150 : 116;
    if (preview == QLatin1String("cover") && r.meta && !r.meta->cover.isEmpty())
        h += coverH;
    else if (preview != QLatin1String("cover") && preview != QLatin1String("none")) {
        QString text;
        if (!cardMedia(m_ctx.view, m_ctx.schema, r, &text).isEmpty())
            h += coverH;
    }
    const QFontMetricsF fm(Theme::uiFont(13.5, QFont::Medium));
    const double textW = colW - 24 - (m_ctx.view.showPageIcon && r.meta && !r.meta->icon.isEmpty() ? 22 : 0);
    const QRectF br = fm.boundingRect(QRectF(0, 0, textW, 400), Qt::TextWordWrap, titleOf(r));
    h += qMin(br.height(), fm.lineSpacing() * 3) + 4;
    for (const Property *p : m_ctx.schema.orderedProps(m_ctx.view)) {
        if (p->type == PropType::Title || p == m_groupProp || p == m_laneProp)
            continue;
        if (isEmptyValue(*p, rowValue(*p, r)) && p->type != PropType::Checkbox && p->type != PropType::Button)
            continue;
        h += 26;
    }
    return h + 10;
}

void BoardView::layoutBoard()
{
    m_columns.clear();
    m_lanes.clear();
    m_groupProp = groupProp();
    m_laneProp = nullptr;
    if (!m_groupProp) {
        // A board needs something to group by: use the first select or status.
        m_width = 0;
        setFixedHeight(220);
        return;
    }
    if (const Property *lp = m_ctx.schema.prop(m_ctx.view.subGroupBy); lp && canGroupBy(*lp) && lp != m_groupProp)
        m_laneProp = lp;
    const QList<RowGroup> groupsList = groups();
    const double colW = columnWidth();
    // Column x positions are shared by every lane.
    QList<double> xs;
    QList<double> ws;
    double x = 0;
    for (const RowGroup &g : groupsList) {
        const bool folded = groupCollapsed(g.key);
        xs.append(x);
        ws.append(folded ? kFoldedW : colW);
        x += ws.last() + kGap;
    }
    m_addGroupRect = propTypeHasOptions(m_groupProp->type) && m_ctx.editable ? QRectF(x, 4, 150, 30) : QRectF();
    m_width = x + (m_addGroupRect.isNull() ? 0 : 160);

    // Lanes: one per sub-group, or a single lane holding every row.
    QList<RowGroup> lanes;
    if (m_laneProp) {
        lanes = groupRows(m_ctx.schema, *m_laneProp, m_ctx.rows, QString(), 10, m_ctx.view.hideEmptyGroups);
    } else {
        RowGroup all;
        for (int i = 0; i < m_ctx.rows.size(); ++i)
            all.rows.append(i);
        lanes.append(all);
    }
    double y = kHeaderH + 6;
    for (int li = 0; li < lanes.size(); ++li) {
        Lane lane;
        lane.group = lanes[li];
        lane.top = y;
        const bool laneFolded = m_laneProp && m_foldedLanes.contains(lane.group.key);
        if (m_laneProp) {
            lane.headerRect = QRectF(0, y, qMax(320.0, qMin(m_width, 600.0)), kLaneH);
            y += kLaneH + 4;
        }
        const QSet<int> inLane(lane.group.rows.begin(), lane.group.rows.end());
        double laneBottom = y;
        for (int gi = 0; gi < groupsList.size(); ++gi) {
            Column col;
            col.group = groupsList[gi];
            col.lane = li;
            col.collapsed = groupCollapsed(col.group.key);
            col.headerRect = QRectF(xs[gi], 0, ws[gi], kHeaderH);
            double cy = y;
            if (!laneFolded && !col.collapsed) {
                for (int r : col.group.rows) {
                    if (!inLane.contains(r))
                        continue;
                    const double h = cardHeight(m_ctx.rows[r]);
                    col.cards.append(Card{r, QRectF(xs[gi], cy, colW, h)});
                    cy += h + 8;
                }
                col.addRect = QRectF(xs[gi], cy, colW, 32);
                cy += 40;
            }
            col.rect = QRectF(xs[gi], y, ws[gi], qMax(cy - y, laneFolded ? 0.0 : 60.0));
            laneBottom = qMax(laneBottom, col.rect.bottom());
            m_columns.append(col);
        }
        lane.bottom = laneBottom;
        m_lanes.append(lane);
        y = laneBottom + (m_laneProp ? 18 : 0);
    }
    // Folded columns stretch over the whole board.
    for (Column &col : m_columns)
        if (col.collapsed && col.lane == 0)
            col.rect = QRectF(col.rect.left(), kHeaderH + 6, col.rect.width(), qMax(160.0, y - kHeaderH - 6));
    setFixedHeight(int(y + 24));
}

QRect BoardView::titleRectFor(const QString &rowId) const
{
    for (const Column &col : m_columns)
        for (const Card &c : col.cards)
            if (m_ctx.rows[c.row].meta && m_ctx.rows[c.row].meta->id == rowId)
                return QRect(mapToGlobal(c.rect.topLeft().toPoint() + QPoint(4, 4)), QSize(int(c.rect.width()) - 8, 30));
    return {};
}

void BoardView::paintCard(QPainter &p, const QRectF &rect, const RowRef &r, bool hover, bool ghost)
{
    const auto &c = Theme::colors();
    p.save();
    if (ghost)
        Theme::paintSoftShadow(p, rect, 10, 14, 1.6);
    Theme::paintCard(p, rect, 10, hover || ghost ? QColor(0x21, 0x21, 0x26) : QColor(0x1A, 0x1A, 0x1E));
    double y = rect.top();
    const double coverH = m_ctx.view.cardSize == QLatin1String("small") ? 90 : m_ctx.view.cardSize == QLatin1String("large") ? 150 : 116;
    const QString preview = m_ctx.view.cardPreview;
    QString media;
    if (preview != QLatin1String("cover") && preview != QLatin1String("none"))
        media = cardMedia(m_ctx.view, m_ctx.schema, r, nullptr);
    const bool hasCover = preview == QLatin1String("cover") && r.meta && !r.meta->cover.isEmpty();
    if (hasCover || !media.isEmpty()) {
        const QRectF area(rect.left() + 1, rect.top() + 1, rect.width() - 2, coverH);
        QPainterPath clip;
        clip.addRoundedRect(area, 9, 9);
        QPainterPath square;
        square.addRect(area.adjusted(0, 20, 0, 0));
        p.save();
        p.setClipPath(clip.united(square));
        if (hasCover) {
            Cosmos::paintCover(p, area, r.meta->cover, r.meta->coverPos);
        } else {
            p.fillRect(area, QColor(0x14, 0x14, 0x18));
            paintPreview(p, area, previewImage(media, area.size(), m_ctx.view.cardFit), m_ctx.view.cardFit);
        }
        p.restore();
        y += coverH;
    }
    y += 12;
    const QFont tf = Theme::uiFont(13.5, QFont::Medium);
    const QFontMetricsF fm(tf);
    double x = rect.left() + 12;
    if (m_ctx.view.showPageIcon && r.meta && !r.meta->icon.isEmpty()) {
        Icons::paintPageIcon(p, QRectF(x, y + 1, 16, 16), r.meta->icon, c.mist);
        x += 22;
    }
    p.setFont(tf);
    p.setPen(r.meta && !r.meta->title.trimmed().isEmpty() ? c.starlight : c.haze);
    const QRectF tr(x, y, rect.right() - 12 - x, qMin(fm.lineSpacing() * 3, 400.0));
    const QRectF used = fm.boundingRect(QRectF(0, 0, tr.width(), 400), Qt::TextWordWrap, titleOf(r));
    p.drawText(QRectF(tr.left(), tr.top(), tr.width(), qMin(used.height(), fm.lineSpacing() * 3)), Qt::TextWordWrap, titleOf(r));
    y += qMin(used.height(), fm.lineSpacing() * 3) + 4;
    for (const Property *prop : m_ctx.schema.orderedProps(m_ctx.view)) {
        if (prop->type == PropType::Title || prop == m_groupProp || prop == m_laneProp)
            continue;
        const QJsonValue v = rowValue(*prop, r);
        if (isEmptyValue(*prop, v) && prop->type != PropType::Checkbox && prop->type != PropType::Button)
            continue;
        PropertyEditors::paintValue(p, QRectF(rect.left() + 12, y + 1, rect.width() - 24, 24), *prop, v);
        y += 26;
    }
    p.restore();
}

void BoardView::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!m_groupProp) {
        paintEmptyState(p, QRectF(0, 0, width(), height()), tr("A board groups cards by a property, like Status."),
                        m_ctx.editable ? tr("Group by Status") : QString(), &m_emptyButton);
        return;
    }
    // Column headers (once, above every lane).
    QSet<QString> drawnHeaders;
    for (int ci = 0; ci < m_columns.size(); ++ci) {
        const Column &col = m_columns[ci];
        if (col.lane != 0)
            continue;
        const RowGroup &g = col.group;
        const bool hover = ci == m_hoverHeader;
        if (col.collapsed) {
            // A folded column: a slim strip with the name written downwards.
            p.setPen(Qt::NoPen);
            p.setBrush(m_ctx.view.colorColumns ? shadeWash(g.shade) : QColor(255, 255, 255, 5));
            p.drawRoundedRect(QRectF(col.headerRect.left(), 0, col.headerRect.width(), col.rect.bottom()), 10, 10);
            if (hover)
                Theme::paintHover(p, QRectF(col.headerRect.left(), 0, col.headerRect.width(), col.rect.bottom()), 0.8, 10);
            Icons::paint(p, QRectF(col.headerRect.center().x() - 7, 12, 14, 14), QStringLiteral("chevron-right"), c.fog);
            p.save();
            p.translate(col.headerRect.center().x() - 8, 40);
            p.rotate(90);
            p.setFont(Theme::uiFont(13, QFont::Medium));
            p.setPen(c.moon);
            const QString text = QStringLiteral("%1   %2").arg(g.label).arg(g.rows.size());
            p.drawText(QRectF(0, -2, 400, 20), Qt::AlignVCenter, text);
            p.restore();
            continue;
        }
        double x = col.headerRect.left() + 4;
        if (hover)
            Theme::paintHover(p, col.headerRect.adjusted(-2, 4, 2, -4), 0.7, 6);
        if (propTypeHasOptions(m_groupProp->type) && !g.key.isEmpty()) {
            if (g.statusGroup >= 0) {
                Icons::paint(p, QRectF(x, col.headerRect.center().y() - 7, 14, 14),
                             g.statusGroup == 2   ? QStringLiteral("check-circle")
                             : g.statusGroup == 1 ? QStringLiteral("status")
                                                  : QStringLiteral("select"),
                             c.mist);
                x += 20;
            }
            x += Theme::paintPill(p, QPointF(x, col.headerRect.center().y() - 11), g.label, g.shade) + 8;
        } else {
            if (m_groupProp->type == PropType::Relation && !g.key.isEmpty()) {
                const PageMeta *m = Workspace::instance()->page(g.key);
                Icons::paintPageIcon(p, QRectF(x, col.headerRect.center().y() - 8, 16, 16), m ? m->icon : QString(), c.mist,
                                     QStringLiteral("page"));
                x += 22;
            } else if (m_groupProp->type == PropType::Person && !g.key.isEmpty()) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.ash);
                p.drawEllipse(QRectF(x, col.headerRect.center().y() - 9, 18, 18));
                p.setFont(Theme::uiFont(10.5, QFont::DemiBold));
                p.setPen(c.starlight);
                p.drawText(QRectF(x, col.headerRect.center().y() - 9, 18, 18), Qt::AlignCenter, g.label.left(1).toUpper());
                x += 24;
            }
            p.setFont(Theme::uiFont(13.5, QFont::DemiBold));
            p.setPen(g.key.isEmpty() ? c.haze : c.starlight);
            const double avail = col.headerRect.right() - x - 60;
            const QString label = QFontMetricsF(p.font()).elidedText(g.label, Qt::ElideRight, avail);
            p.drawText(QRectF(x, col.headerRect.top(), avail, col.headerRect.height()), Qt::AlignVCenter, label);
            x += QFontMetricsF(p.font()).horizontalAdvance(label) + 10;
        }
        p.setFont(Theme::uiFont(12.5, QFont::Medium));
        p.setPen(c.fog);
        p.drawText(QRectF(x, col.headerRect.top(), 40, col.headerRect.height()), Qt::AlignVCenter, QString::number(g.rows.size()));
        if (hover) {
            Icons::paint(p, QRectF(col.headerRect.right() - 46, col.headerRect.center().y() - 7, 14, 14), QStringLiteral("more"), c.fog);
            Icons::paint(p, QRectF(col.headerRect.right() - 22, col.headerRect.center().y() - 7, 14, 14), QStringLiteral("plus"), c.fog);
        }
        drawnHeaders.insert(g.key);
    }
    // Lanes.
    for (int li = 0; li < m_lanes.size(); ++li) {
        const Lane &lane = m_lanes[li];
        if (!m_laneProp)
            continue;
        const bool folded = m_foldedLanes.contains(lane.group.key);
        paintGroupHeader(p, lane.headerRect.adjusted(6, 0, 0, 0), lane.group, m_laneProp, folded, li == m_hoverLane);
    }
    for (int ci = 0; ci < m_columns.size(); ++ci) {
        const Column &col = m_columns[ci];
        if (col.collapsed)
            continue;
        const bool laneFolded = m_laneProp && m_foldedLanes.contains(m_lanes[col.lane].group.key);
        if (laneFolded)
            continue;
        p.setPen(Qt::NoPen);
        QColor wash = m_ctx.view.colorColumns ? shadeWash(propTypeHasOptions(m_groupProp->type) ? col.group.shade : -1) : QColor(255, 255, 255, 4);
        if (m_dragging && ci == m_dropColumn)
            wash = wash.lighter(160), wash.setAlpha(wash.alpha() + 12);
        p.setBrush(wash);
        const QRectF wr = m_laneProp ? col.rect.adjusted(-6, -4, 6, 4) : QRectF(col.rect.left() - 6, 0 - 2, col.rect.width() + 12, col.rect.bottom() + 6);
        p.drawRoundedRect(wr, 12, 12);
        for (int k = 0; k < col.cards.size(); ++k) {
            const Card &card = col.cards[k];
            const bool dragged = m_dragging && m_dragCard == QPair<int, int>(ci, k);
            if (dragged) {
                p.setPen(QPen(c.ash, 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(card.rect.adjusted(1, 1, -1, -1), 10, 10);
                continue;
            }
            paintCard(p, card.rect, m_ctx.rows[card.row], m_hoverCard == QPair<int, int>(ci, k), false);
        }
        if (m_dragging && ci == m_dropColumn) {
            const double y = col.cards.isEmpty()                ? col.rect.top() + 2
                             : m_dropIndex < col.cards.size() ? col.cards[m_dropIndex].rect.top() - 4
                                                                : col.cards.last().rect.bottom() + 4;
            p.setPen(Qt::NoPen);
            p.setBrush(c.mist);
            p.drawRoundedRect(QRectF(col.rect.left(), y - 1.5, col.rect.width(), 3), 1.5, 1.5);
        }
        if (!col.addRect.isNull()) {
            const bool hoverAdd = m_hoverAdd == ci;
            if (hoverAdd)
                Theme::paintHover(p, col.addRect, 1.0, 7);
            Icons::paint(p, QRectF(col.addRect.left() + 10, col.addRect.center().y() - 7, 14, 14), QStringLiteral("plus"), hoverAdd ? c.moon : c.fog);
            p.setFont(Theme::uiFont(13.5));
            p.setPen(hoverAdd ? c.moon : c.fog);
            p.drawText(col.addRect.adjusted(32, 0, 0, 0), Qt::AlignVCenter, tr("New"));
        }
    }
    if (!m_addGroupRect.isNull()) {
        if (m_hoverAddGroup)
            Theme::paintHover(p, m_addGroupRect, 1.0, 7);
        Icons::paint(p, QRectF(m_addGroupRect.left() + 10, m_addGroupRect.center().y() - 7, 14, 14), QStringLiteral("plus"),
                     m_hoverAddGroup ? c.moon : c.fog);
        p.setFont(Theme::uiFont(13.5));
        p.setPen(m_hoverAddGroup ? c.moon : c.fog);
        p.drawText(m_addGroupRect.adjusted(32, 0, 0, 0), Qt::AlignVCenter, tr("Add group"));
    }
    if (m_dragging && m_dragCard.first >= 0) {
        const Card &card = m_columns[m_dragCard.first].cards[m_dragCard.second];
        p.setOpacity(0.95);
        p.translate(m_dragPos - m_dragOffset - card.rect.topLeft());
        p.rotate(1.2);
        paintCard(p, card.rect, m_ctx.rows[card.row], true, true);
    }
}

int BoardView::columnAt(const QPointF &pos) const
{
    for (int i = 0; i < m_columns.size(); ++i) {
        const Column &col = m_columns[i];
        if (col.collapsed)
            continue;
        const Lane &lane = m_lanes[col.lane];
        const double top = m_laneProp ? lane.top : 0;
        const double bottom = m_laneProp ? lane.bottom + 18 : height();
        if (pos.x() >= col.rect.left() - kGap / 2 && pos.x() < col.rect.right() + kGap / 2 && pos.y() >= top && pos.y() < bottom)
            return i;
    }
    return -1;
}

void BoardView::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    if ((e->buttons() & Qt::LeftButton) && m_dragCard.first >= 0) {
        if (!m_dragging && (e->position().toPoint() - m_pressPos).manhattanLength() > 5)
            m_dragging = true;
        if (m_dragging) {
            m_dragPos = pos;
            m_dropColumn = columnAt(pos);
            if (m_dropColumn >= 0) {
                const Column &col = m_columns[m_dropColumn];
                m_dropIndex = int(col.cards.size());
                for (int i = 0; i < col.cards.size(); ++i)
                    if (pos.y() < col.cards[i].rect.center().y()) {
                        m_dropIndex = i;
                        break;
                    }
            }
            update();
        }
        return;
    }
    QPair<int, int> hover{-1, -1};
    int hoverAdd = -1, hoverHeader = -1, hoverLane = -1;
    for (int ci = 0; ci < m_columns.size(); ++ci) {
        const Column &col = m_columns[ci];
        for (int k = 0; k < col.cards.size(); ++k)
            if (col.cards[k].rect.contains(pos))
                hover = {ci, k};
        if (col.addRect.contains(pos))
            hoverAdd = ci;
        if (col.lane == 0 && (col.headerRect.contains(pos) || (col.collapsed && QRectF(col.rect.left(), 0, col.rect.width(), col.rect.bottom()).contains(pos))))
            hoverHeader = ci;
    }
    for (int li = 0; li < m_lanes.size(); ++li)
        if (m_laneProp && m_lanes[li].headerRect.contains(pos))
            hoverLane = li;
    const bool addGroup = m_addGroupRect.contains(pos);
    if (hover != m_hoverCard || hoverAdd != m_hoverAdd || addGroup != m_hoverAddGroup || hoverHeader != m_hoverHeader || hoverLane != m_hoverLane) {
        m_hoverCard = hover;
        m_hoverAdd = hoverAdd;
        m_hoverAddGroup = addGroup;
        m_hoverHeader = hoverHeader;
        m_hoverLane = hoverLane;
        update();
    }
    setCursor(hover.first >= 0 || hoverAdd >= 0 || addGroup || hoverHeader >= 0 || hoverLane >= 0 || (!m_groupProp && m_emptyButton.contains(pos))
                  ? Qt::PointingHandCursor
                  : Qt::ArrowCursor);
}

void BoardView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPointF pos = e->position();
    if (!m_groupProp) {
        if (m_emptyButton.contains(pos) && m_ctx.editable) {
            const QString existing = m_ctx.schema.defaultGroupProp();
            if (!existing.isEmpty())
                saveView([existing](View &v) { v.groupBy = existing; });
            else
                addPropertyOfType(m_ctx.databaseId, m_ctx.view.id, PropType::Status, true);
        }
        return;
    }
    if (m_hoverLane >= 0) {
        const QString key = m_lanes[m_hoverLane].group.key;
        if (m_foldedLanes.contains(key))
            m_foldedLanes.remove(key);
        else
            m_foldedLanes.insert(key);
        layoutBoard();
        emit geometryNeedsUpdate();
        update();
        return;
    }
    if (m_hoverHeader >= 0) {
        const Column &col = m_columns[m_hoverHeader];
        if (col.collapsed) {
            toggleGroup(col.group.key);
            return;
        }
        const QString key = col.group.key;
        if (pos.x() >= col.headerRect.right() - 26) {
            emit createRow(groupValues(m_groupProp, key), false);
            return;
        }
        // The column menu: fold, hide, and for options rename/delete via the property.
        QMenu *menu = Theme::makeMenu(this);
        menu->addAction(Icons::icon(QStringLiteral("chevron-left")), tr("Fold column"), this, [this, key] { toggleGroup(key); });
        if (m_ctx.editable)
            menu->addAction(Icons::icon(QStringLiteral("eye-off")), tr("Hide group"), this, [this, key] {
                saveView([key](View &v) {
                    if (!v.hiddenGroups.contains(key))
                        v.hiddenGroups.append(key);
                });
            });
        if (m_ctx.editable && propTypeHasOptions(m_groupProp->type) && !key.isEmpty()) {
            const QString propId = m_groupProp->id;
            const QString db = m_ctx.databaseId;
            menu->addSeparator();
            menu->addAction(Icons::icon(QStringLiteral("pencil")), tr("Rename option…"), this, [this, propId, db, key] {
                Workspace *ws = Workspace::instance();
                Schema s = ws->schema(db);
                Property *p = s.prop(propId);
                if (!p)
                    return;
                for (Option &o : p->options)
                    if (o.id == key) {
                        QString name = o.name;
                        if (textDialog(window(), tr("Rename option"), tr("A new name for this group."), &name) && !name.trimmed().isEmpty()) {
                            o.name = name.trimmed();
                            ws->setSchema(db, s);
                        }
                        return;
                    }
            });
        }
        menu->popup(mapToGlobal(QPoint(int(col.headerRect.left()), int(col.headerRect.bottom()))));
        return;
    }
    if (m_hoverAddGroup) {
        QString name;
        if (textDialog(window(), tr("New group"), tr("A new option for “%1”.").arg(m_groupProp->name), &name) && !name.trimmed().isEmpty()) {
            Workspace *ws = Workspace::instance();
            Schema s = ws->schema(m_ctx.databaseId);
            if (Property *p = s.prop(m_groupProp->id))
                parseValue(*p, name.trimmed(), true);
            ws->setSchema(m_ctx.databaseId, s);
        }
        return;
    }
    if (m_hoverAdd >= 0) {
        const Column &col = m_columns[m_hoverAdd];
        QJsonObject props = groupValues(m_groupProp, col.group.key);
        if (m_laneProp) {
            const QJsonObject lane = groupValues(m_laneProp, m_lanes[col.lane].group.key);
            for (auto it = lane.begin(); it != lane.end(); ++it)
                props.insert(it.key(), it.value());
        }
        emit createRow(props, false);
        return;
    }
    if (m_hoverCard.first >= 0) {
        m_dragCard = m_hoverCard;
        m_pressPos = e->position().toPoint();
        m_dragOffset = pos - m_columns[m_dragCard.first].cards[m_dragCard.second].rect.topLeft();
    }
}

void BoardView::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragCard.first < 0)
        return;
    const Column &from = m_columns[m_dragCard.first];
    const Card card = from.cards[m_dragCard.second];
    const RowRef row = m_ctx.rows[card.row];
    if (!m_dragging) {
        if (row.meta)
            emit openRow(row.meta->id);
    } else if (m_dropColumn >= 0 && row.meta) {
        Workspace *ws = Workspace::instance();
        const Column &target = m_columns[m_dropColumn];
        QString before;
        for (int i = m_dropIndex; i < target.cards.size(); ++i) {
            const RowRef &r = m_ctx.rows[target.cards[i].row];
            if (r.meta && r.meta->id != row.meta->id) {
                before = r.meta->id;
                break;
            }
        }
        const QString id = row.meta->id;
        const QString fromKey = from.group.key, toKey = target.group.key;
        const QString fromLane = m_laneProp ? m_lanes[from.lane].group.key : QString();
        const QString toLane = m_laneProp ? m_lanes[target.lane].group.key : QString();
        const bool sorted = m_ctx.sorted;
        ws->batch([&] {
            moveToGroup(row, m_groupProp, fromKey, toKey);
            if (m_laneProp && fromLane != toLane) {
                // moveToGroup reads values from the row; refresh them after the first move.
                const RowRef fresh{row.meta, ws->props(id)};
                moveToGroup(fresh, m_laneProp, fromLane, toLane);
            }
            if (!sorted)
                ws->moveRow(id, before);
        });
    }
    m_dragging = false;
    m_dragCard = {-1, -1};
    m_dropColumn = -1;
    update();
}

void BoardView::leaveEvent(QEvent *)
{
    m_hoverCard = {-1, -1};
    m_hoverAdd = m_hoverHeader = m_hoverLane = -1;
    m_hoverAddGroup = false;
    update();
}

void BoardView::contextMenuEvent(QContextMenuEvent *e)
{
    for (const Column &col : m_columns)
        for (const Card &c : col.cards)
            if (c.rect.contains(e->pos()) && m_ctx.rows[c.row].meta) {
                rowMenu(m_ctx.rows[c.row].meta->id, e->globalPos());
                return;
            }
}
