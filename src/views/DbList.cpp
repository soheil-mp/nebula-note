// The list and gallery layouts, both with groups.
#include "DbViews.h"
#include "DbViewsPrivate.h"

#include "PropertyEditors.h"
#include "core/Workspace.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

using namespace DbViewsPrivate;

namespace
{
constexpr double kListRowH = 40;
constexpr double kListGroupH = 44;
} // namespace

// ---- ListView -------------------------------------------------------------------

ListView::ListView(QWidget *parent)
    : DbView(parent)
{
}

void ListView::setContext(const ViewContext &ctx)
{
    DbView::setContext(ctx);
    layoutLines();
    update();
}

void ListView::layoutLines()
{
    m_lines.clear();
    m_groups = groups();
    double y = 0;
    const int limit = rowLimit();
    int shown = 0;
    bool truncated = false;
    auto addRows = [&](const QList<int> &indexes, const QString &group, int gi) {
        QList<RowRef> subset;
        for (int i : indexes)
            subset.append(m_ctx.rows[i]);
        for (const NestedRow &n : nestRows(m_ctx.schema, subset, m_collapsedRows, m_ctx.view.subItems)) {
            if (shown >= limit) {
                truncated = true;
                break;
            }
            Line l{Line::Row, y, indexes[n.index], n.depth, n.children, group, gi};
            m_lines.append(l);
            y += kListRowH;
            ++shown;
        }
        Line add{Line::Add, y};
        add.group = group;
        add.groupIndex = gi;
        m_lines.append(add);
        y += kListRowH;
    };
    if (m_groups.isEmpty()) {
        QList<int> all;
        for (int i = 0; i < m_ctx.rows.size(); ++i)
            all.append(i);
        addRows(all, QString(), -1);
    } else {
        for (int gi = 0; gi < m_groups.size(); ++gi) {
            Line gl{Line::Group, y + 6};
            gl.group = m_groups[gi].key;
            gl.groupIndex = gi;
            m_lines.append(gl);
            y += kListGroupH + 6;
            if (!groupCollapsed(m_groups[gi].key))
                addRows(m_groups[gi].rows, m_groups[gi].key, gi);
        }
    }
    if (truncated) {
        m_lines.append({Line::More, y});
        y += kListRowH;
    }
    setFixedHeight(int(y + 12));
}

int ListView::lineAt(double y) const
{
    for (int i = 0; i < m_lines.size(); ++i) {
        const double h = m_lines[i].kind == Line::Group ? kListGroupH : kListRowH;
        if (y >= m_lines[i].y && y < m_lines[i].y + h)
            return i;
    }
    return -1;
}

QRect ListView::titleRectFor(const QString &rowId) const
{
    for (const Line &l : m_lines)
        if (l.kind == Line::Row && m_ctx.rows[l.row].meta && m_ctx.rows[l.row].meta->id == rowId)
            return QRect(mapToGlobal(QPoint(28 + l.depth * 22, int(l.y))), QSize(qMin(400, width() - 40), int(kListRowH)));
    return {};
}

void ListView::paintEvent(QPaintEvent *e)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QList<const Property *> props = m_ctx.schema.orderedProps(m_ctx.view);
    for (int li = 0; li < m_lines.size(); ++li) {
        const Line &line = m_lines[li];
        if (line.y > e->rect().bottom() || line.y + kListGroupH < e->rect().top())
            continue;
        const bool hover = li == m_hover;
        if (line.kind == Line::Group) {
            const RowGroup &g = m_groups[line.groupIndex];
            paintGroupHeader(p, QRectF(6, line.y, qMin(560.0, width() - 12.0), kListGroupH), g, groupProp(), groupCollapsed(g.key), hover);
            continue;
        }
        const QRectF row(0, line.y, width(), kListRowH);
        if (line.kind == Line::Add || line.kind == Line::More) {
            if (hover)
                Theme::paintHover(p, row.adjusted(0, 2, 0, -2), 1.0, 7);
            Icons::paint(p, QRectF(12, row.center().y() - 7, 14, 14), line.kind == Line::Add ? QStringLiteral("plus") : QStringLiteral("chevron-down"),
                         hover ? c.moon : c.fog);
            p.setFont(Theme::uiFont(13.5));
            p.setPen(hover ? c.moon : c.fog);
            p.drawText(row.adjusted(38, 0, 0, 0), Qt::AlignVCenter, line.kind == Line::Add ? tr("New") : tr("Load more"));
            continue;
        }
        const RowRef &r = m_ctx.rows[line.row];
        if (hover)
            Theme::paintHover(p, row.adjusted(0, 2, 0, -2), 1.0, 7);
        double x = 10 + line.depth * 22;
        if (line.children > 0) {
            const bool folded = r.meta && m_collapsedRows.contains(r.meta->id);
            Icons::paint(p, QRectF(x - 2, row.center().y() - 7, 14, 14), folded ? QStringLiteral("chevron-right") : QStringLiteral("chevron-down"),
                         hover && m_hoverToggle ? c.moon : c.fog);
            x += 16;
        } else if (line.depth > 0) {
            x += 16;
        }
        if (m_ctx.view.showPageIcon) {
            Icons::paintPageIcon(p, QRectF(x, row.center().y() - 9, 18, 18), r.meta ? r.meta->icon : QString(), c.mist, QStringLiteral("page"));
            x += 28;
        }
        // Properties on the right, from the right edge.
        double right = row.right() - 10;
        for (auto it = props.crbegin(); it != props.crend(); ++it) {
            const Property *prop = *it;
            if (prop->type == PropType::Title)
                continue;
            const QJsonValue v = rowValue(*prop, r);
            if (isEmptyValue(*prop, v))
                continue;
            const double w = qMin(220.0, PropertyEditors::valueWidth(*prop, v) + 4);
            if (right - w < row.width() * 0.45)
                break;
            PropertyEditors::paintValue(p, QRectF(right - w, row.top(), w, row.height()), *prop, v, true);
            right -= w + 14;
        }
        p.setFont(Theme::uiFont(14, QFont::Medium));
        p.setPen(r.meta && !r.meta->title.trimmed().isEmpty() ? c.moon : c.haze);
        p.drawText(QRectF(x, row.top(), right - x - 10, row.height()), Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(titleOf(r), Qt::ElideRight, right - x - 10));
        p.setPen(QPen(c.hairline, 1));
        p.drawLine(QPointF(0, row.bottom() - 0.5), QPointF(width(), row.bottom() - 0.5));
    }
}

void ListView::mouseMoveEvent(QMouseEvent *e)
{
    const int h = lineAt(e->position().y());
    bool toggle = false;
    if (h >= 0 && m_lines[h].kind == Line::Row && m_lines[h].children > 0) {
        const double x = 10 + m_lines[h].depth * 22;
        toggle = e->position().x() >= x - 4 && e->position().x() < x + 14;
    }
    if (h != m_hover || toggle != m_hoverToggle) {
        m_hover = h;
        m_hoverToggle = toggle;
        update();
    }
    setCursor(h < 0 ? Qt::ArrowCursor : Qt::PointingHandCursor);
}

void ListView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_hover < 0)
        return;
    const Line &line = m_lines[m_hover];
    switch (line.kind) {
    case Line::Group:
        toggleGroup(m_groups[line.groupIndex].key);
        break;
    case Line::Add:
        emit createRow(line.groupIndex >= 0 ? groupValues(groupProp(), line.group) : QJsonObject(), false);
        break;
    case Line::More:
        emit loadMore();
        break;
    case Line::Row: {
        const RowRef &r = m_ctx.rows[line.row];
        if (!r.meta)
            break;
        if (m_hoverToggle) {
            if (m_collapsedRows.contains(r.meta->id))
                m_collapsedRows.remove(r.meta->id);
            else
                m_collapsedRows.insert(r.meta->id);
            layoutLines();
            emit geometryNeedsUpdate();
            update();
        } else {
            emit openRow(r.meta->id);
        }
        break;
    }
    }
}

void ListView::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void ListView::contextMenuEvent(QContextMenuEvent *e)
{
    const int i = lineAt(e->pos().y());
    if (i >= 0 && m_lines[i].kind == Line::Row && m_ctx.rows[m_lines[i].row].meta)
        rowMenu(m_ctx.rows[m_lines[i].row].meta->id, e->globalPos());
}

// ---- GalleryView ----------------------------------------------------------------

GalleryView::GalleryView(QWidget *parent)
    : DbView(parent)
{
}

void GalleryView::setContext(const ViewContext &ctx)
{
    DbView::setContext(ctx);
    m_previewCache.clear();
    layoutCards();
    update();
}

void GalleryView::resizeEvent(QResizeEvent *)
{
    layoutCards();
}

QString GalleryView::previewMedia(const RowRef &row) const
{
    if (!row.meta)
        return {};
    auto it = m_previewCache.find(row.meta->id);
    if (it == m_previewCache.end()) {
        QString text;
        const QString media = cardMedia(m_ctx.view, m_ctx.schema, row, &text);
        it = m_previewCache.insert(row.meta->id, {media, text});
    }
    return it->first;
}

QString GalleryView::previewText(const QString &rowId) const
{
    return m_previewCache.value(rowId).second;
}

void GalleryView::layoutCards()
{
    m_cards.clear();
    m_headers.clear();
    m_groups = groups();
    m_moreRect = QRectF();
    const QString size = m_ctx.view.cardSize;
    const double minW = size == QLatin1String("small") ? 190 : size == QLatin1String("large") ? 330 : 250;
    m_coverH = m_ctx.view.cardPreview == QLatin1String("none") ? 0
               : size == QLatin1String("small")                ? 110
               : size == QLatin1String("large")                ? 200
                                                               : 150;
    const double gap = 14;
    const double W = qMax(200, width());
    const int cols = qMax(1, int((W + gap) / (minW + gap)));
    const double cw = (W - gap * (cols - 1)) / cols;
    int visibleProps = 0;
    for (const Property *p : m_ctx.schema.orderedProps(m_ctx.view))
        if (p->type != PropType::Title)
            ++visibleProps;
    const double ch = m_coverH + 46 + visibleProps * 26 + (visibleProps ? 6 : 0);
    const int limit = rowLimit();
    int shown = 0;
    bool truncated = false;
    double y = 0;
    auto place = [&](const QList<int> &indexes, const QString &group) {
        QList<int> cards;
        for (int i : indexes) {
            if (shown >= limit) {
                truncated = true;
                break;
            }
            cards.append(i);
            ++shown;
        }
        cards.append(-1);
        for (int k = 0; k < cards.size(); ++k) {
            const double cardH = cards[k] < 0 ? qMin(ch, 120.0) : ch;
            m_cards.append({cards[k], QRectF((k % cols) * (cw + gap), y + (k / cols) * (ch + gap), cw, cardH), group});
        }
        const int rowsN = (int(cards.size()) + cols - 1) / cols;
        y += rowsN * (ch + gap);
    };
    if (m_groups.isEmpty()) {
        QList<int> all;
        for (int i = 0; i < m_ctx.rows.size(); ++i)
            all.append(i);
        place(all, QString());
    } else {
        for (int gi = 0; gi < m_groups.size(); ++gi) {
            m_headers.append({gi, QRectF(0, y + 4, qMin(560.0, W), 40)});
            y += 52;
            if (!groupCollapsed(m_groups[gi].key))
                place(m_groups[gi].rows, m_groups[gi].key);
        }
    }
    if (truncated) {
        m_moreRect = QRectF(0, y, W, 36);
        y += 40;
    }
    setFixedHeight(int(y + 10));
}

QRect GalleryView::titleRectFor(const QString &rowId) const
{
    for (const Card &c : m_cards)
        if (c.row >= 0 && m_ctx.rows[c.row].meta && m_ctx.rows[c.row].meta->id == rowId)
            return QRect(mapToGlobal(QPoint(int(c.rect.left()), int(c.rect.top() + m_coverH))), QSize(int(c.rect.width()), 40));
    return {};
}

void GalleryView::paintEvent(QPaintEvent *e)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int hi = 0; hi < m_headers.size(); ++hi) {
        const RowGroup &g = m_groups[m_headers[hi].groupIndex];
        paintGroupHeader(p, m_headers[hi].rect.adjusted(6, 0, 0, 0), g, groupProp(), groupCollapsed(g.key), hi == m_hoverHeader);
    }
    if (!m_moreRect.isNull()) {
        if (m_hoverMore)
            Theme::paintHover(p, m_moreRect, 1.0, 7);
        Icons::paint(p, QRectF(m_moreRect.left() + 12, m_moreRect.center().y() - 7, 14, 14), QStringLiteral("chevron-down"), m_hoverMore ? c.moon : c.fog);
        p.setFont(Theme::uiFont(13.5));
        p.setPen(m_hoverMore ? c.moon : c.fog);
        p.drawText(m_moreRect.adjusted(36, 0, 0, 0), Qt::AlignVCenter, tr("Load more"));
    }
    for (int i = 0; i < m_cards.size(); ++i) {
        const QRectF r = m_cards[i].rect;
        if (!r.intersects(e->rect()))
            continue;
        const bool hover = i == m_hover;
        if (m_cards[i].row < 0) {
            QPen dash(hover ? c.smoke : c.ash, 1, Qt::DashLine);
            dash.setDashPattern({4, 3});
            p.setPen(dash);
            p.setBrush(hover ? QColor(255, 255, 255, 6) : QColor(Qt::transparent));
            p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
            Icons::paint(p, QRectF(r.center().x() - 9, r.center().y() - 18, 18, 18), QStringLiteral("plus"), hover ? c.moon : c.fog);
            p.setFont(Theme::uiFont(13.5));
            p.setPen(hover ? c.moon : c.fog);
            p.drawText(QRectF(r.left(), r.center().y() + 4, r.width(), 20), Qt::AlignCenter, tr("New"));
            continue;
        }
        const RowRef &row = m_ctx.rows[m_cards[i].row];
        if (hover)
            Theme::paintSoftShadow(p, r, 12, 10, 1.0);
        Theme::paintCard(p, r, 12, hover ? QColor(0x1F, 0x1F, 0x24) : QColor(0x1A, 0x1A, 0x1E));
        if (m_coverH > 0) {
            const QRectF cover(r.left() + 1, r.top() + 1, r.width() - 2, m_coverH);
            QPainterPath clip;
            clip.addRoundedRect(cover, 11, 11);
            QPainterPath square;
            square.addRect(cover.adjusted(0, 20, 0, 0));
            p.save();
            p.setClipPath(clip.united(square));
            const QString preview = m_ctx.view.cardPreview;
            const QString media = previewMedia(row);
            if (preview == QLatin1String("cover") && row.meta && !row.meta->cover.isEmpty()) {
                Cosmos::paintCover(p, cover, row.meta->cover, row.meta->coverPos);
            } else if (!media.isEmpty()) {
                p.fillRect(cover, QColor(0x14, 0x14, 0x18));
                paintPreview(p, cover, previewImage(media, cover.size(), m_ctx.view.cardFit), m_ctx.view.cardFit);
            } else {
                p.fillRect(cover, QColor(0x14, 0x14, 0x18));
                const QString text = row.meta ? previewText(row.meta->id) : QString();
                if (!text.isEmpty()) {
                    p.setFont(Theme::uiFont(11.5));
                    p.setPen(c.fog);
                    p.drawText(cover.adjusted(14, 12, -14, -8), Qt::TextWordWrap, text);
                } else {
                    Cosmos::paintStars(p, cover.toRect(), this, 0.6);
                }
            }
            p.restore();
            p.setPen(QPen(c.hairline, 1));
            p.drawLine(QPointF(cover.left(), cover.bottom() + 0.5), QPointF(cover.right(), cover.bottom() + 0.5));
        }
        double y = r.top() + m_coverH + 12;
        double x = r.left() + 14;
        if (m_ctx.view.showPageIcon && row.meta && !row.meta->icon.isEmpty()) {
            Icons::paintPageIcon(p, QRectF(x, y + 2, 18, 18), row.meta->icon, c.mist);
            x += 24;
        }
        p.setFont(Theme::uiFont(14, QFont::DemiBold));
        p.setPen(row.meta && !row.meta->title.trimmed().isEmpty() ? c.starlight : c.haze);
        p.drawText(QRectF(x, y, r.right() - 14 - x, 22), Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(titleOf(row), Qt::ElideRight, r.right() - 14 - x));
        y += 30;
        for (const Property *prop : m_ctx.schema.orderedProps(m_ctx.view)) {
            if (prop->type == PropType::Title)
                continue;
            PropertyEditors::paintValue(p, QRectF(r.left() + 14, y, r.width() - 28, 24), *prop, rowValue(*prop, row));
            y += 26;
        }
    }
}

void GalleryView::mouseMoveEvent(QMouseEvent *e)
{
    int h = -1, hh = -1;
    for (int i = 0; i < m_cards.size(); ++i)
        if (m_cards[i].rect.contains(e->position()))
            h = i;
    for (int i = 0; i < m_headers.size(); ++i)
        if (m_headers[i].rect.contains(e->position()))
            hh = i;
    const bool more = m_moreRect.contains(e->position());
    if (h != m_hover || hh != m_hoverHeader || more != m_hoverMore) {
        m_hover = h;
        m_hoverHeader = hh;
        m_hoverMore = more;
        setCursor(h >= 0 || hh >= 0 || more ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void GalleryView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_hoverMore) {
        emit loadMore();
        return;
    }
    if (m_hoverHeader >= 0) {
        toggleGroup(m_groups[m_headers[m_hoverHeader].groupIndex].key);
        return;
    }
    if (m_hover < 0)
        return;
    const Card &card = m_cards[m_hover];
    if (card.row < 0)
        emit createRow(groupValues(groupProp(), card.group), true);
    else if (m_ctx.rows[card.row].meta)
        emit openRow(m_ctx.rows[card.row].meta->id);
}

void GalleryView::leaveEvent(QEvent *)
{
    m_hover = m_hoverHeader = -1;
    m_hoverMore = false;
    update();
}

void GalleryView::contextMenuEvent(QContextMenuEvent *e)
{
    for (const Card &c : m_cards)
        if (c.row >= 0 && c.rect.contains(e->pos()) && m_ctx.rows[c.row].meta)
            rowMenu(m_ctx.rows[c.row].meta->id, e->globalPos());
}
