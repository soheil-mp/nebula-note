// Shared parts of the database layouts: row menus, grouping and moving rows
// between groups.
#include "DbViews.h"
#include "DbViewsPrivate.h"

#include "PropertyEditors.h"
#include "core/Blocks.h"
#include "core/Workspace.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QCache>
#include <QClipboard>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>

using DbViewsPrivate::tr;

namespace DbViewsPrivate
{
void paintEmptyState(QPainter &p, const QRectF &rect, const QString &text, const QString &button, QRectF *buttonRect)
{
    const auto &c = Theme::colors();
    Cosmos::paintPlanet(p, QPointF(rect.center().x(), rect.top() + 46), 16, 0.9, true);
    p.setFont(Theme::uiFont(13.5));
    p.setPen(c.haze);
    p.drawText(QRectF(rect.left(), rect.top() + 78, rect.width(), 22), Qt::AlignCenter, text);
    if (!button.isEmpty() && buttonRect) {
        const double w = QFontMetricsF(Theme::uiFont(13, QFont::Medium)).horizontalAdvance(button) + 28;
        *buttonRect = QRectF(rect.center().x() - w / 2, rect.top() + 110, w, 30);
        p.setPen(QPen(c.ash, 1));
        p.setBrush(c.graphite);
        p.drawRoundedRect(*buttonRect, 7, 7);
        p.setFont(Theme::uiFont(13, QFont::Medium));
        p.setPen(c.moon);
        p.drawText(*buttonRect, Qt::AlignCenter, button);
    }
}

void addPropertyOfType(const QString &dbId, const QString &viewId, PropType type, bool groupBy)
{
    Workspace *ws = Workspace::instance();
    Schema s = ws->schema(dbId);
    Property p;
    p.id = s.newPropId();
    p.type = type;
    p.name = propTypeName(type);
    if (type == PropType::Status)
        p.options = {{QStringLiteral("todo"), tr("Not started"), 2, 0},
                     {QStringLiteral("doing"), tr("In progress"), 5, 1},
                     {QStringLiteral("done"), tr("Done"), 8, 2}};
    s.props.append(p);
    if (View *v = s.view(viewId)) {
        if (groupBy)
            v->groupBy = p.id;
        else
            v->dateProp = p.id;
    }
    ws->setSchema(dbId, s);
}

QImage previewImage(const QString &media, const QSizeF &size, bool fit)
{
    static QCache<QString, QImage> cache(60);
    if (media.isEmpty() || !Workspace::instance())
        return {};
    const QSize px = (size * 2).toSize();
    const QString key = QStringLiteral("%1|%2x%3|%4").arg(media).arg(px.width()).arg(px.height()).arg(fit);
    if (QImage *hit = cache.object(key))
        return *hit;
    const QString path = QFileInfo(media).isAbsolute() ? media : Workspace::instance()->mediaPath(media);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize natural = reader.size();
    if (natural.isValid())
        reader.setScaledSize(natural.scaled(px, fit ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding));
    const QImage img = reader.read();
    cache.insert(key, new QImage(img));
    return img;
}

void paintPreview(QPainter &p, const QRectF &area, const QImage &img, bool fit)
{
    if (img.isNull())
        return;
    const QSizeF s = QSizeF(img.size()).scaled(area.size(), fit ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding);
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setClipRect(area, Qt::IntersectClip);
    p.drawImage(QRectF(area.center().x() - s.width() / 2, area.center().y() - s.height() / 2, s.width(), s.height()), img);
    p.restore();
}

QString cardMedia(const View &view, const Schema &schema, const RowRef &row, QString *text)
{
    if (!row.meta || !Workspace::instance())
        return {};
    const QString preview = view.cardPreview;
    if (preview == QLatin1String("none") || preview == QLatin1String("cover"))
        return {};
    if (const Property *files = schema.prop(preview); files && files->type == PropType::Files) {
        for (const FileRef &f : filesOf(row.props.value(files->id))) {
            const QString suffix = QFileInfo(f.media.isEmpty() ? f.url : f.media).suffix().toLower();
            if (!f.media.isEmpty() && QImageReader::supportedImageFormats().contains(suffix.toLatin1()))
                return f.media;
        }
        return {};
    }
    // Page content: the first image, else its text.
    const QList<Block> blocks = Workspace::instance()->content(row.meta->id);
    for (const Block &b : blocks)
        if (b.type == BlockType::Image && !b.media.isEmpty())
            return b.media;
    if (text)
        *text = blocksPlainText(blocks).left(400);
    return {};
}

QColor shadeWash(int shade)
{
    if (shade < 0)
        return QColor(255, 255, 255, 5);
    const Theme::Pill pill = Theme::optionPill(shade);
    QColor c = pill.fill.alpha() == 0 ? pill.border : pill.fill;
    c.setAlpha(shade >= 7 ? 16 : 34);
    return c;
}
} // namespace DbViewsPrivate

DbView::DbView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
}

void DbView::setContext(const ViewContext &ctx)
{
    m_ctx = ctx;
}

void DbView::setScrollOffset(int x)
{
    if (x == m_scrollX)
        return;
    m_scrollX = x;
    onScroll();
}

QString DbView::titleOf(const RowRef &r) const
{
    return r.meta && !r.meta->title.trimmed().isEmpty() ? r.meta->title : tr("Untitled");
}

void DbView::saveView(const std::function<void(View &)> &fn)
{
    if (m_ctx.editView) {
        m_ctx.editView(fn);
        return;
    }
    Workspace *ws = Workspace::instance();
    Schema s = ws->schema(m_ctx.databaseId);
    if (View *v = s.view(m_ctx.view.id)) {
        fn(*v);
        ws->setSchema(m_ctx.databaseId, s);
    }
}

void DbView::rowMenu(const QString &rowId, const QPoint &globalPos)
{
    QMenu *menu = Theme::makeMenu(this);
    QAction *open = menu->addAction(Icons::icon(QStringLiteral("external")), tr("Open"));
    connect(open, &QAction::triggered, this, [this, rowId] { emit openRow(rowId); });
    QAction *rename = menu->addAction(Icons::icon(QStringLiteral("pencil")), tr("Rename"));
    connect(rename, &QAction::triggered, this, [this, rowId] {
        const QRect r = titleRectFor(rowId);
        editTitleInline(rowId, r.isEmpty() ? QRect(QCursor::pos(), QSize(260, 30)) : r);
    });
    QAction *dup = menu->addAction(Icons::icon(QStringLiteral("copy")), tr("Duplicate"));
    connect(dup, &QAction::triggered, this, [rowId] { Workspace::instance()->duplicate(rowId); });
    QAction *link = menu->addAction(Icons::icon(QStringLiteral("link")), tr("Copy link"));
    connect(link, &QAction::triggered, this, [rowId] { QApplication::clipboard()->setText(QStringLiteral("nebula://page/") + rowId); });
    // Sub-items.
    if (const Property *parent = m_ctx.schema.prop(m_ctx.schema.subItemsProp)) {
        const QString parentProp = parent->id;
        const QString dbId = m_ctx.databaseId;
        QAction *sub = menu->addAction(Icons::icon(QStringLiteral("subitems")), tr("Add sub-item"));
        connect(sub, &QAction::triggered, this, [rowId, parentProp, dbId] {
            Workspace::instance()->createRow(dbId, QString(), {{parentProp, QJsonArray{rowId}}});
        });
    }
    menu->addSeparator();
    QAction *del = menu->addAction(Icons::icon(QStringLiteral("trash")), tr("Delete"));
    connect(del, &QAction::triggered, this, [rowId] { Workspace::instance()->trash(rowId); });
    menu->popup(globalPos);
}

void DbView::editTitleInline(const QString &rowId, const QRect &globalRect)
{
    const Property *t = m_ctx.schema.titleProp();
    if (!t)
        return;
    PropertyEditors::edit(this, globalRect.topLeft() - QPoint(8, 8), m_ctx.databaseId, rowId, t->id, globalRect.width());
}

const Property *DbView::groupProp() const
{
    const Property *p = m_ctx.schema.prop(m_ctx.view.groupBy);
    return p && canGroupBy(*p) ? p : nullptr;
}

QList<RowGroup> DbView::groups() const
{
    const Property *p = groupProp();
    if (!p)
        return {};
    QList<RowGroup> gs = groupRows(m_ctx.schema, *p, m_ctx.rows, m_ctx.view.groupMode, m_ctx.view.groupStep, m_ctx.view.hideEmptyGroups);
    gs.removeIf([&](const RowGroup &g) { return m_ctx.view.hiddenGroups.contains(g.key); });
    return gs;
}

bool DbView::groupCollapsed(const QString &key) const
{
    return m_ctx.view.collapsedGroups.contains(key.isEmpty() ? QStringLiteral("__none") : key);
}

void DbView::toggleGroup(const QString &key)
{
    const QString k = key.isEmpty() ? QStringLiteral("__none") : key;
    if (m_ctx.view.collapsedGroups.contains(k))
        m_ctx.view.collapsedGroups.removeAll(k);
    else
        m_ctx.view.collapsedGroups.append(k);
    const QStringList folded = m_ctx.view.collapsedGroups;
    saveView([folded](View &v) { v.collapsedGroups = folded; });
}

bool DbView::canSetGroup(const Property *p) const
{
    if (!p || propTypeIsComputed(p->type) || p->type == PropType::Title)
        return false;
    switch (p->type) {
    case PropType::Select:
    case PropType::Status:
    case PropType::MultiSelect:
    case PropType::Checkbox:
    case PropType::Person:
    case PropType::Relation:
    case PropType::Text:
    case PropType::Url:
    case PropType::Email:
    case PropType::Phone:
        return true;
    case PropType::Date:
        // Day groups map to a day; other date groupings are ranges.
        return m_ctx.view.groupMode == QLatin1String("day");
    case PropType::Number:
        return false;
    default:
        return false;
    }
}

QJsonObject DbView::groupValues(const Property *p, const QString &key) const
{
    QJsonObject out;
    if (!p || key.isEmpty() || !canSetGroup(p))
        return out;
    switch (p->type) {
    case PropType::Select:
    case PropType::Status:
        out.insert(p->id, key);
        break;
    case PropType::MultiSelect:
    case PropType::Person:
    case PropType::Relation:
        out.insert(p->id, QJsonArray{key});
        break;
    case PropType::Checkbox:
        out.insert(p->id, key == QLatin1String("true"));
        break;
    case PropType::Date:
        out.insert(p->id, key);
        break;
    default:
        out.insert(p->id, key);
        break;
    }
    return out;
}

void DbView::moveToGroup(const RowRef &row, const Property *p, const QString &fromKey, const QString &toKey)
{
    if (!row.meta || !p || !canSetGroup(p) || fromKey == toKey)
        return;
    Workspace *ws = Workspace::instance();
    const QString id = row.meta->id;
    QJsonValue value;
    switch (p->type) {
    case PropType::MultiSelect:
    case PropType::Person:
    case PropType::Relation: {
        QStringList items;
        for (const QJsonValue &v : row.props.value(p->id).toArray())
            items.append(v.toString());
        items.removeAll(fromKey);
        if (!toKey.isEmpty() && !items.contains(toKey))
            items.append(toKey);
        value = items.isEmpty() ? QJsonValue() : QJsonValue(QJsonArray::fromStringList(items));
        break;
    }
    case PropType::Checkbox:
        value = toKey == QLatin1String("true");
        break;
    default:
        value = toKey.isEmpty() ? QJsonValue() : QJsonValue(toKey);
        break;
    }
    ws->setProp(id, p->id, value);
}

int DbView::rowLimit() const
{
    return m_ctx.limit > 0 && !showsAllRows() ? m_ctx.limit : INT_MAX;
}

void DbView::paintGroupHeader(QPainter &p, const QRectF &rect, const RowGroup &g, const Property *prop, bool collapsed, bool hover)
{
    const auto &c = Theme::colors();
    if (hover)
        Theme::paintHover(p, rect.adjusted(-4, 2, 4, -2), 1.0, 6);
    Icons::paint(p, QRectF(rect.left(), rect.center().y() - 7, 14, 14), collapsed ? QStringLiteral("chevron-right") : QStringLiteral("chevron-down"),
                 c.fog);
    double x = rect.left() + 20;
    if (prop && propTypeHasOptions(prop->type) && !g.key.isEmpty()) {
        if (g.statusGroup >= 0) {
            Icons::paint(p, QRectF(x, rect.center().y() - 7, 14, 14),
                         g.statusGroup == 2 ? QStringLiteral("check-circle") : g.statusGroup == 1 ? QStringLiteral("status") : QStringLiteral("select"),
                         c.mist);
            x += 20;
        }
        x += Theme::paintPill(p, QPointF(x, rect.center().y() - 11), g.label, g.shade) + 8;
    } else if (prop && (prop->type == PropType::Relation) && !g.key.isEmpty()) {
        const PageMeta *m = Workspace::instance() ? Workspace::instance()->page(g.key) : nullptr;
        Icons::paintPageIcon(p, QRectF(x, rect.center().y() - 8, 16, 16), m ? m->icon : QString(), c.mist, QStringLiteral("page"));
        x += 22;
        p.setFont(Theme::uiFont(13.5, QFont::DemiBold));
        p.setPen(c.starlight);
        p.drawText(QRectF(x, rect.top(), rect.width(), rect.height()), Qt::AlignVCenter, g.label);
        x += QFontMetricsF(p.font()).horizontalAdvance(g.label) + 10;
    } else {
        p.setFont(Theme::uiFont(13.5, QFont::DemiBold));
        p.setPen(g.key.isEmpty() ? c.haze : c.starlight);
        p.drawText(QRectF(x, rect.top(), rect.width(), rect.height()), Qt::AlignVCenter, g.label);
        x += QFontMetricsF(p.font()).horizontalAdvance(g.label) + 10;
    }
    p.setFont(Theme::uiFont(12.5, QFont::Medium));
    p.setPen(c.fog);
    p.drawText(QRectF(x, rect.top(), 60, rect.height()), Qt::AlignVCenter, QString::number(g.rows.size()));
}
