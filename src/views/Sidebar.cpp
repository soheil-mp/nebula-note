#include "Sidebar.h"

#include "core/Schema.h"
#include "core/Settings.h"
#include "core/Workspace.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>

namespace
{
constexpr int kRowH = 30;
constexpr int kHeaderH = 34;
constexpr int kIndent = 14;
}

// ---- PageTree --------------------------------------------------------------------

PageTree::PageTree(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::TabFocus); // F6 (or Tab) reaches it; clicks leave the focus in the page
    if (Workspace *ws = Workspace::instance()) {
        connect(ws, &Workspace::treeChanged, this, &PageTree::rebuild);
        connect(ws, &Workspace::favoritesChanged, this, &PageTree::rebuild);
        connect(ws, &Workspace::pageChanged, this, [this](const QString &id) {
            for (Row &r : m_rows)
                if (r.id == id && r.kind == Row::Page)
                    r.title = Workspace::instance()->title(id);
            update();
        });
        // Views listed under an expanded database follow its schema.
        connect(ws, &Workspace::schemaChanged, this, [this](const QString &id) {
            for (const Row &r : m_rows)
                if (r.kind == Row::View && r.id == id) {
                    rebuild();
                    return;
                }
        });
    }
}

void PageTree::appendPages(const QString &parentId, int depth, bool favorites)
{
    Workspace *ws = Workspace::instance();
    const QStringList &expanded = Settings::instance().expanded;
    const QList<const PageMeta *> kids = ws->children(parentId);
    if (kids.isEmpty() && depth > 0) {
        Row empty;
        empty.kind = Row::Empty;
        empty.depth = depth;
        empty.h = 28;
        m_rows.append(empty);
        return;
    }
    for (const PageMeta *m : kids) {
        Row r;
        r.id = m->id;
        r.key = (favorites ? QStringLiteral("f:") : QString()) + m->id;
        r.title = ws->title(m->id);
        r.depth = depth;
        r.hasChildren = ws->hasChildren(m->id) || m->isDatabase();
        r.expanded = expanded.contains(r.key);
        r.favorites = favorites;
        m_rows.append(r);
        if (r.expanded && depth < 12) {
            // A database lists its views first, like Notion's sidebar.
            if (m->isDatabase())
                appendViews(m->id, depth + 1);
            if (!m->isDatabase() || ws->hasChildren(m->id))
                appendPages(m->id, depth + 1, favorites);
        }
    }
}

void PageTree::appendViews(const QString &databaseId, int depth)
{
    for (const View &v : Workspace::instance()->schema(databaseId).views) {
        Row r;
        r.kind = Row::View;
        r.id = databaseId;
        r.viewId = v.id;
        r.viewIcon = viewTypeIcon(v.type);
        r.title = v.name;
        r.depth = depth;
        r.h = 28;
        m_rows.append(r);
    }
}

void PageTree::rebuild()
{
    Workspace *ws = Workspace::instance();
    if (!ws)
        return;
    if (m_rename)
        finishRename(false);
    m_rows.clear();
    const QStringList &expanded = Settings::instance().expanded;
    const QList<const PageMeta *> favs = ws->favorites();
    if (!favs.isEmpty()) {
        Row h;
        h.kind = Row::Header;
        h.title = tr("Favorites");
        h.favorites = true;
        h.h = kHeaderH;
        m_rows.append(h);
        for (const PageMeta *m : favs) {
            Row r;
            r.id = m->id;
            r.key = QStringLiteral("f:") + m->id;
            r.title = ws->title(m->id);
            r.hasChildren = ws->hasChildren(m->id) || m->isDatabase();
            r.expanded = expanded.contains(r.key);
            r.favorites = true;
            m_rows.append(r);
            if (r.expanded) {
                if (m->isDatabase())
                    appendViews(m->id, 1);
                if (!m->isDatabase() || ws->hasChildren(m->id))
                    appendPages(m->id, 1, true);
            }
        }
    }
    Row pages;
    pages.kind = Row::Header;
    pages.title = tr("Pages");
    pages.h = kHeaderH;
    m_rows.append(pages);
    appendPages(QString(), 0, false);
    int y = 0;
    for (Row &r : m_rows) {
        r.y = y;
        y += r.h;
    }
    setFixedHeight(y + 12);
    m_hover = -1;
    // The keyboard focus stays on the same row.
    m_focusRow = -1;
    for (int i = 0; i < m_rows.size() && !m_focusKey.isEmpty(); ++i)
        if (m_rows[i].key + m_rows[i].viewId == m_focusKey)
            m_focusRow = i;
    Accessible::itemsChanged(this);
    update();
}

QString PageTree::accessibleItemName(int index) const
{
    const Row &r = m_rows.value(index);
    if (r.kind == Row::Empty)
        return tr("No pages inside");
    return r.title.isEmpty() && r.kind == Row::Page ? Workspace::instance()->title(r.id) : r.title;
}

QAccessible::State PageTree::accessibleItemState(int index) const
{
    QAccessible::State s;
    const Row &r = m_rows.value(index);
    if (r.kind == Row::Page && r.hasChildren) {
        s.expandable = true;
        s.expanded = r.expanded;
        s.collapsed = !r.expanded;
    }
    s.selected = r.kind == Row::Page && r.id == m_current;
    if (r.kind == Row::Header || r.kind == Row::Empty)
        s.disabled = true;
    return s;
}

QRect PageTree::accessibleItemRect(int index) const
{
    const Row &r = m_rows.value(index);
    return QRect(0, r.y, width(), r.h);
}

bool PageTree::focusable(int index) const
{
    return index >= 0 && index < m_rows.size() && (m_rows[index].kind == Row::Page || m_rows[index].kind == Row::View);
}

void PageTree::focusRow(int index)
{
    if (!focusable(index))
        return;
    m_focusRow = index;
    m_focusKey = m_rows[index].key + m_rows[index].viewId;
    emit scrollTo(m_rows[index].y + m_rows[index].h / 2);
    Accessible::focusItem(this, index);
    update();
}

void PageTree::activate(int index)
{
    if (!focusable(index))
        return;
    const Row &r = m_rows[index];
    if (r.kind == Row::View)
        emit openView(r.id, r.viewId);
    else
        emit openPage(r.id);
}

void PageTree::keyPressEvent(QKeyEvent *e)
{
    const int count = int(m_rows.size());
    auto step = [&](int from, int dir) {
        for (int i = from + dir; i >= 0 && i < count; i += dir)
            if (focusable(i))
                return i;
        return from;
    };
    const Row r = m_rows.value(m_focusRow);
    switch (e->key()) {
    case Qt::Key_Down:
        focusRow(step(m_focusRow, 1));
        return;
    case Qt::Key_Up:
        focusRow(step(m_focusRow, -1));
        return;
    case Qt::Key_Home:
        focusRow(step(-1, 1));
        return;
    case Qt::Key_End:
        focusRow(step(count, -1));
        return;
    case Qt::Key_Right:
        if (r.kind == Row::Page && r.hasChildren && !r.expanded)
            toggle(r);
        else if (r.expanded)
            focusRow(step(m_focusRow, 1));
        return;
    case Qt::Key_Left:
        if (r.kind == Row::Page && r.expanded) {
            toggle(r);
        } else {
            // Up to the parent row.
            for (int i = m_focusRow - 1; i >= 0; --i)
                if (focusable(i) && m_rows[i].depth < r.depth) {
                    focusRow(i);
                    break;
                }
        }
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        activate(m_focusRow);
        return;
    case Qt::Key_F2:
        if (r.kind == Row::Page)
            renamePage(r.id);
        return;
    case Qt::Key_Menu:
        if (r.kind == Row::Page)
            emit pageMenu(r.id, mapToGlobal(QPoint(40, r.y + r.h)));
        return;
    default:
        if (e->key() == Qt::Key_F10 && (e->modifiers() & Qt::ShiftModifier) && r.kind == Row::Page) {
            emit pageMenu(r.id, mapToGlobal(QPoint(40, r.y + r.h)));
            return;
        }
        QWidget::keyPressEvent(e);
    }
}

void PageTree::focusInEvent(QFocusEvent *e)
{
    Theme::noteFocusReason(this, e->reason());
    QWidget::focusInEvent(e);
    // Start on the open page, or the first one.
    if (!focusable(m_focusRow)) {
        int start = -1;
        for (int i = 0; i < m_rows.size(); ++i)
            if (focusable(i) && (start < 0 || (m_rows[i].kind == Row::Page && m_rows[i].id == m_current && !m_rows[i].favorites)))
                start = i;
        if (start >= 0)
            focusRow(start);
    } else {
        Accessible::focusItem(this, m_focusRow);
    }
    update();
}

void PageTree::focusOutEvent(QFocusEvent *e)
{
    QWidget::focusOutEvent(e);
    update();
}

void PageTree::setCurrent(const QString &id)
{
    m_current = id;
    update();
}

void PageTree::revealPage(const QString &id)
{
    Workspace *ws = Workspace::instance();
    if (!ws || !ws->exists(id))
        return;
    QStringList &expanded = Settings::instance().expanded;
    bool changed = false;
    for (const QString &a : ws->ancestors(id)) {
        if (!expanded.contains(a)) {
            expanded.append(a);
            changed = true;
        }
    }
    if (changed)
        rebuild();
    for (const Row &r : m_rows)
        if (r.id == id && !r.favorites)
            emit scrollTo(r.y + r.h / 2);
}

int PageTree::rowAt(int y) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (y >= m_rows[i].y && y < m_rows[i].y + m_rows[i].h)
            return i;
    return -1;
}

QRect PageTree::chevronRect(const Row &r) const
{
    // Shares its spot with the page icon, lined up with the items above the tree.
    return QRect(11 + r.depth * kIndent, r.y + (r.h - 20) / 2, 20, 20);
}

QRect PageTree::plusRect(const Row &r) const
{
    return QRect(width() - 32, r.y + (r.h - 22) / 2, 22, 22);
}

QRect PageTree::moreRect(const Row &r) const
{
    return QRect(width() - 56, r.y + (r.h - 22) / 2, 22, 22);
}

QRegion PageTree::inkRegion() const
{
    QRegion ink;
    for (const Row &r : m_rows) {
        int left = 0, textLeft = 0;
        QFont font = Theme::uiFont(13.5);
        if (r.kind == Row::Header) {
            left = textLeft = 12;
            font = Theme::uiFont(11.5, QFont::Medium);
        } else if (r.kind == Row::Empty) {
            left = textLeft = 14 + r.depth * kIndent;
            font = Theme::uiFont(12.5);
        } else if (r.kind == Row::View) {
            left = 12 + r.depth * kIndent;
            textLeft = left + 28;
            font = Theme::uiFont(13);
        } else {
            left = chevronRect(r).left();
            textLeft = left + 29;
        }
        const QString text = r.kind == Row::Empty ? tr("No pages inside") : r.title;
        const int right = qMin(width() - 8, textLeft + int(QFontMetricsF(font).horizontalAdvance(text)) + 4);
        const int top = r.kind == Row::Header ? r.y + 10 : r.y + 4;
        ink += QRect(left - 3, top, right - left + 6, r.y + r.h - 4 - top);
    }
    return ink;
}

void PageTree::paintEvent(QPaintEvent *e)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Workspace *ws = Workspace::instance();
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows[i];
        if (r.y > e->rect().bottom() || r.y + r.h < e->rect().top())
            continue;
        const QRect rowRect(4, r.y + 1, width() - 8, r.h - 2);
        if (r.kind == Row::Header) {
            p.setFont(Theme::uiFont(11.5, QFont::Medium));
            p.setPen(c.fog);
            p.drawText(QRect(12, r.y + 8, width() - 60, r.h - 8), Qt::AlignVCenter | Qt::AlignLeft, r.title);
            if (!r.favorites && i == m_hover) {
                const QRect plus(width() - 32, r.y + 8 + (r.h - 8 - 22) / 2, 22, 22);
                if (m_hoverPart == 4)
                    Theme::paintHover(p, plus, 1.2, 5);
                Icons::paint(p, QRectF(plus).adjusted(4, 4, -4, -4), QStringLiteral("plus"), m_hoverPart == 4 ? c.starlight : c.haze);
            }
            continue;
        }
        if (r.kind == Row::Empty) {
            p.setFont(Theme::uiFont(12.5));
            p.setPen(c.dust);
            p.drawText(QRect(14 + r.depth * kIndent, r.y, width() - 40, r.h), Qt::AlignVCenter, tr("No pages inside"));
            continue;
        }
        if (r.kind == Row::View) {
            const bool hoverView = i == m_hover && !m_dragging;
            if (hoverView)
                Theme::paintHover(p, rowRect, 0.9, 6);
            const int x = 12 + r.depth * kIndent;
            Icons::paint(p, QRectF(x + 1.5, r.y + (r.h - 15) / 2.0, 15, 15), r.viewIcon, hoverView ? c.starlight : c.fog);
            p.setFont(Theme::uiFont(13));
            p.setPen(hoverView ? c.starlight : c.haze);
            p.drawText(QRectF(x + 28, r.y, width() - x - 40, r.h), Qt::AlignVCenter,
                       QFontMetricsF(p.font()).elidedText(r.title, Qt::ElideRight, width() - x - 40));
            continue;
        }
        const bool current = r.id == m_current;
        const bool hover = i == m_hover;
        // The row being dragged stays behind, faded.
        p.setOpacity(m_dragging && i == m_pressRow ? 0.4 : 1.0);
        if (current) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.075));
            p.drawRoundedRect(rowRect, 6, 6);
        } else if (hover && !m_dragging) {
            Theme::paintHover(p, rowRect, 0.9, 6);
        }
        if (m_dragging && i == m_dropRow) {
            p.setPen(Qt::NoPen);
            if (m_dropMode == 0) {
                // Into this page: an outlined row, unlike a plain hover.
                p.setBrush(Theme::withAlpha(c.mist, 0.14));
                p.setPen(QPen(Theme::withAlpha(c.mist, 0.7), 1));
                p.drawRoundedRect(QRectF(rowRect).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
                p.setPen(Qt::NoPen);
            } else {
                const int y = m_dropMode < 0 ? r.y : r.y + r.h;
                p.setBrush(c.mist);
                p.drawRoundedRect(QRectF(10 + r.depth * kIndent, y - 1.5, width() - 20 - r.depth * kIndent, 3), 1.5, 1.5);
            }
        }
        const PageMeta *m = ws->page(r.id);
        const QRect chev = chevronRect(r);
        const QRect icon(chev.left() + 1, r.y + (r.h - 18) / 2, 18, 18);
        // The icon turns into a chevron on hover, as in Notion.
        if (hover && !m_dragging) {
            if (m_hoverPart == 1)
                Theme::paintHover(p, chev, 1.3, 4);
            Icons::paint(p, QRectF(chev).adjusted(3, 3, -3, -3), r.expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
                         m_hoverPart == 1 ? c.starlight : c.mist);
        } else {
            Icons::paintPageIcon(p, icon, m ? m->icon : QString(), current ? c.starlight : c.mist,
                                 m && m->isDatabase() ? QStringLiteral("database") : QStringLiteral("page"));
        }
        double right = width() - 12;
        if (hover && !m_dragging) {
            right = moreRect(r).left() - 2;
            for (int part : {2, 3}) {
                const QRect pr = part == 2 ? plusRect(r) : moreRect(r);
                if (m_hoverPart == part)
                    Theme::paintHover(p, pr, 1.3, 5);
                Icons::paint(p, QRectF(pr).adjusted(4, 4, -4, -4), part == 2 ? QStringLiteral("plus") : QStringLiteral("more"),
                             m_hoverPart == part ? c.starlight : c.haze);
            }
        }
        p.setFont(Theme::uiFont(13.5, current ? QFont::Medium : QFont::Normal));
        p.setPen(current ? c.starlight : (m && m->title.trimmed().isEmpty() ? c.fog : c.moon));
        const double x = chev.left() + 29;
        p.drawText(QRectF(x, r.y, right - x, r.h), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetricsF(p.font()).elidedText(r.title, Qt::ElideRight, right - x));
        p.setOpacity(1.0);
    }
    // The dragged page follows the pointer as a small ghost.
    if (m_dragging && m_pressRow >= 0 && m_pressRow < m_rows.size()) {
        const Row &dr = m_rows[m_pressRow];
        const PageMeta *dm = ws->page(dr.id);
        p.setFont(Theme::uiFont(13, QFont::Medium));
        const QString text = QFontMetricsF(p.font()).elidedText(dr.title, Qt::ElideRight, 150);
        const double w = QFontMetricsF(p.font()).horizontalAdvance(text) + 40;
        const QRectF ghostRect(qMin(m_dragPos.x() + 16.0, width() - w - 4), m_dragPos.y() + 10, w, 26);
        p.setOpacity(0.92);
        Theme::paintSoftShadow(p, ghostRect, 7, 6, 1.0);
        p.setPen(QPen(c.ash, 1));
        p.setBrush(c.graphite);
        p.drawRoundedRect(ghostRect.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        Icons::paintPageIcon(p, QRectF(ghostRect.left() + 8, ghostRect.center().y() - 8, 16, 16), dm ? dm->icon : QString(), c.mist,
                             dm && dm->isDatabase() ? QStringLiteral("database") : QStringLiteral("page"));
        p.setPen(c.starlight);
        p.drawText(ghostRect.adjusted(30, 0, -8, 0), Qt::AlignVCenter, text);
        p.setOpacity(1.0);
    }
    if (Theme::showsFocus(this) && focusable(m_focusRow)) {
        const Row &r = m_rows[m_focusRow];
        Theme::paintFocusRing(p, QRectF(4, r.y + 1, width() - 8, r.h - 2), 6);
    }
}

void PageTree::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    if (m_pressRow >= 0 && (e->buttons() & Qt::LeftButton)) {
        if (!m_dragging && (pos - m_pressPos).manhattanLength() > 6)
            m_dragging = true;
        if (m_dragging) {
            m_dragPos = pos;
            const int i = rowAt(pos.y());
            m_dropRow = -1;
            if (i >= 0 && m_rows[i].kind == Row::Page && !m_rows[i].favorites) {
                const Row &r = m_rows[i];
                const int dy = pos.y() - r.y;
                m_dropMode = dy < r.h * 0.28 ? -1 : (dy > r.h * 0.72 ? 1 : 0);
                // Not into itself or its own children.
                if (!Workspace::instance()->isInside(r.id, m_rows[m_pressRow].id))
                    m_dropRow = i;
            }
            update();
            setCursor(Qt::ClosedHandCursor);
        }
        return;
    }
    const int i = rowAt(pos.y());
    int part = 0;
    if (i >= 0) {
        const Row &r = m_rows[i];
        if (r.kind == Row::Header && !r.favorites && QRect(width() - 32, r.y + 8, 22, r.h - 8).contains(pos))
            part = 4;
        else if (r.kind == Row::Page) {
            if (chevronRect(r).contains(pos))
                part = 1;
            else if (plusRect(r).contains(pos))
                part = 2;
            else if (moreRect(r).contains(pos))
                part = 3;
        }
    }
    if (i != m_hover || part != m_hoverPart) {
        m_hover = i;
        m_hoverPart = part;
        update();
    }
    const bool clickable = i >= 0 && (m_rows[i].kind == Row::Page || m_rows[i].kind == Row::View || part == 4);
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (i >= 0 && part == 2)
        setToolTip(tr("Add a page inside"));
    else if (i >= 0 && part == 3)
        setToolTip(tr("Delete, duplicate, and more…"));
    else if (part == 4)
        setToolTip(tr("Add a page"));
    else
        setToolTip(QString());
}

void PageTree::toggle(const Row &r)
{
    QStringList &expanded = Settings::instance().expanded;
    if (expanded.contains(r.key))
        expanded.removeAll(r.key);
    else
        expanded.append(r.key);
    Settings::instance().save();
    rebuild();
}

void PageTree::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPoint pos = e->position().toPoint();
    const int i = rowAt(pos.y());
    if (i < 0)
        return;
    const Row r = m_rows[i];
    if (r.kind == Row::Header) {
        if (m_hoverPart == 4)
            emit newPage(QString());
        return;
    }
    if (r.kind == Row::View) {
        emit openView(r.id, r.viewId);
        return;
    }
    if (r.kind != Row::Page)
        return;
    switch (m_hoverPart) {
    case 1:
        toggle(r);
        return;
    case 2: {
        QStringList &expanded = Settings::instance().expanded;
        if (!expanded.contains(r.key))
            expanded.append(r.key);
        emit newPage(r.id);
        return;
    }
    case 3:
        emit pageMenu(r.id, mapToGlobal(moreRect(r).bottomLeft()));
        return;
    default:
        m_pressRow = i;
        m_pressPos = pos;
    }
}

void PageTree::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_pressRow < 0)
        return;
    const Row pressed = m_rows.value(m_pressRow);
    if (m_dragging) {
        if (m_dropRow >= 0) {
            Workspace *ws = Workspace::instance();
            const Row target = m_rows[m_dropRow];
            if (m_dropMode == 0) {
                ws->move(pressed.id, target.id, -1);
                QStringList &expanded = Settings::instance().expanded;
                if (!expanded.contains(target.key))
                    expanded.append(target.key);
            } else {
                const PageMeta *tm = ws->page(target.id);
                const QString parent = tm ? tm->parentId : QString();
                int index = 0;
                const auto siblings = ws->children(parent);
                for (int k = 0; k < siblings.size(); ++k)
                    if (siblings[k]->id == target.id)
                        index = k;
                // Moving down within the same parent shifts indexes by one.
                int from = -1;
                for (int k = 0; k < siblings.size(); ++k)
                    if (siblings[k]->id == pressed.id)
                        from = k;
                if (m_dropMode > 0)
                    ++index;
                if (from >= 0 && from < index)
                    --index;
                ws->move(pressed.id, parent, index);
            }
        }
    } else if (pressed.kind == Row::Page && rowAt(e->position().toPoint().y()) == m_pressRow) {
        emit openPage(pressed.id);
    }
    m_pressRow = -1;
    m_dragging = false;
    m_dropRow = -1;
    setCursor(Qt::ArrowCursor);
    update();
}

void PageTree::mouseDoubleClickEvent(QMouseEvent *e)
{
    const int i = rowAt(e->position().toPoint().y());
    if (i >= 0 && m_rows[i].kind == Row::Page && m_hoverPart == 0)
        renamePage(m_rows[i].id);
}

void PageTree::renamePage(const QString &id)
{
    for (const Row &r : m_rows) {
        if (r.id != id || r.kind != Row::Page)
            continue;
        if (m_rename)
            finishRename(false);
        m_renameId = id;
        m_rename = new QLineEdit(this);
        m_rename->setText(Workspace::instance()->page(id)->title);
        m_rename->setFont(Theme::uiFont(13.5));
        const int x = chevronRect(r).left() + 25;
        m_rename->setGeometry(x, r.y + 2, width() - x - 8, r.h - 4);
        m_rename->show();
        m_rename->setFocus();
        m_rename->selectAll();
        connect(m_rename, &QLineEdit::returnPressed, this, [this] { finishRename(true); });
        connect(m_rename, &QLineEdit::editingFinished, this, [this] { finishRename(true); });
        return;
    }
}

void PageTree::finishRename(bool commit)
{
    if (!m_rename)
        return;
    QLineEdit *edit = m_rename;
    m_rename = nullptr;
    if (commit)
        Workspace::instance()->setTitle(m_renameId, edit->text().trimmed());
    edit->deleteLater();
}

void PageTree::leaveEvent(QEvent *)
{
    m_hover = -1;
    m_hoverPart = 0;
    update();
}

void PageTree::contextMenuEvent(QContextMenuEvent *e)
{
    const int i = rowAt(e->pos().y());
    if (i >= 0 && m_rows[i].kind == Row::Page)
        emit pageMenu(m_rows[i].id, e->globalPos());
}

// ---- Sidebar ---------------------------------------------------------------------

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);
    m_tree = new PageTree;
    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_tree);
    m_tree->setAutoFillBackground(false); // setWidget turns it on
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameStyle(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setAutoFillBackground(false);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    connect(m_tree, &PageTree::openPage, this, &Sidebar::openPage);
    connect(m_tree, &PageTree::openView, this, &Sidebar::openView);
    connect(m_tree, &PageTree::newPage, this, &Sidebar::newPage);
    connect(m_tree, &PageTree::pageMenu, this, &Sidebar::pageMenuRequested);
    connect(m_tree, &PageTree::scrollTo, this, [this](int y) { m_scroll->ensureVisible(0, y, 0, 60); });

    m_top = {{QStringLiteral("search"), QStringLiteral("search"), tr("Search"), QStringLiteral("Ctrl+P"), {}},
             {QStringLiteral("home"), QStringLiteral("home"), tr("Home"), QString(), {}},
             {QStringLiteral("import"), QStringLiteral("download"), tr("Import"), QString(), {}}};
    m_bottom = {{QStringLiteral("settings"), QStringLiteral("settings"), tr("Settings"), QStringLiteral("Ctrl+,"), {}},
                {QStringLiteral("trash"), QStringLiteral("trash"), tr("Trash"), QString(), {}}};
    m_tree->rebuild();
    if (Workspace *ws = Workspace::instance()) {
        connect(ws, &Workspace::pageChanged, this, [this](const QString &id) {
            if (id.isEmpty())
                update(); // workspace renamed
        });
    }
    connect(&Settings::instance(), &Settings::changed, this, qOverload<>(&QWidget::update));
}

void Sidebar::setCurrentPage(const QString &id)
{
    // Rows aren't in the tree: highlight their database instead.
    QString shown = id;
    if (const PageMeta *m = Workspace::instance() ? Workspace::instance()->page(id) : nullptr; m && m->isRow())
        shown = m->parentId;
    setHomeActive(false);
    m_tree->setCurrent(shown);
    m_tree->revealPage(shown);
}

void Sidebar::setHomeActive(bool active)
{
    m_homeActive = active;
    if (active)
        m_tree->setCurrent(QString());
    update();
}

void Sidebar::layoutItems()
{
    m_headerRect = QRect(8, 10, width() - 16, 36);
    m_collapseRect = QRect(width() - 66, 15, 26, 26);
    m_newRect = QRect(width() - 38, 15, 26, 26);
    int y = 54;
    for (Item &it : m_top) {
        it.rect = QRect(6, y, width() - 12, 30);
        y += 31;
    }
    const int top = y + 8;
    int by = height() - 12 - int(m_bottom.size()) * 31;
    for (Item &it : m_bottom) {
        it.rect = QRect(6, by, width() - 12, 30);
        by += 31;
    }
    m_scroll->setGeometry(2, top, width() - 4, height() - top - int(m_bottom.size()) * 31 - 22);
}

void Sidebar::resizeEvent(QResizeEvent *)
{
    layoutItems();
}

void Sidebar::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    const Settings &settings = Settings::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!window()->isMaximized() && !window()->isFullScreen()) {
        // Follow the window's rounded left corners.
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, width() + Theme::kRadius, height()), Theme::kRadius - 1, Theme::kRadius - 1);
        p.setClipPath(clip);
    }
    // Translucent where the compositor blurs behind us, solid otherwise.
    QColor base = c.abyss;
    if (settings.translucentSidebar)
        base.setAlphaF(0.82f);
    QLinearGradient g(QPointF(0, 0), QPointF(0, height()));
    g.setColorAt(0, Theme::withAlpha(QColor(0x12, 0x12, 0x16), base.alphaF()));
    g.setColorAt(1, Theme::withAlpha(QColor(0x0B, 0x0B, 0x0D), base.alphaF()));
    p.fillRect(rect(), g);
    // Stars fill the empty sky between rows, never the labels themselves.
    QRegion ink;
    {
        const QString name = Workspace::instance() ? Workspace::instance()->name() : QStringLiteral("Nebula Note");
        const double nameW = QFontMetricsF(Theme::uiFont(14, QFont::DemiBold)).horizontalAdvance(name);
        ink += QRect(m_headerRect.left(), m_headerRect.center().y() - 14, int(qMin(36 + nameW + 6, double(m_headerRect.width()))), 28);
        const QFontMetricsF fm(Theme::uiFont(13.5, QFont::Medium));
        for (const QList<Item> *items : {&m_top, &m_bottom})
            for (const Item &it : *items)
                ink += QRect(it.rect.left() + 5, it.rect.top() + 4, int(36 + fm.horizontalAdvance(it.text)), it.rect.height() - 8);
        const QRect treeArea(m_scroll->viewport()->mapTo(this, QPoint(0, 0)), m_scroll->viewport()->size());
        ink += m_tree->inkRegion().translated(m_tree->mapTo(this, QPoint(0, 0))).intersected(treeArea);
    }
    Cosmos::paintStars(p, rect(), this, 0.55 * settings.starfield, ink);
    Cosmos::paintNebulaHaze(p, QRectF(-width() * 0.4, height() * 0.55, width() * 1.6, height() * 0.6), 0.9 * settings.starfield, 3);
    // Hairline separating sidebar and page.
    p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.06), 1));
    p.drawLine(QPointF(width() - 0.5, 0), QPointF(width() - 0.5, height()));

    // Workspace header.
    if (m_hover == 200)
        Theme::paintHover(p, m_headerRect, 1.0, 8);
    const QString name = Workspace::instance() ? Workspace::instance()->name() : QStringLiteral("Nebula Note");
    {
        // The workspace mark: its initial on a small lit tile (the full app
        // icon turns to mud at this size).
        const QRectF tile(m_headerRect.left() + 5, m_headerRect.center().y() - 11, 22, 22);
        QLinearGradient tg(tile.topLeft(), tile.bottomLeft());
        tg.setColorAt(0, QColor(0x3A, 0x3A, 0x42));
        tg.setColorAt(1, QColor(0x22, 0x22, 0x28));
        p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.12), 1));
        p.setBrush(tg);
        p.drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        QString initial;
        for (const QChar ch : name)
            if (ch.isLetterOrNumber()) {
                initial = ch.toUpper();
                break;
            }
        p.setFont(Theme::uiFont(12, QFont::Bold));
        p.setPen(c.starlight);
        p.drawText(tile.adjusted(0, 0, 0, -0.5), Qt::AlignCenter, initial.isEmpty() ? QStringLiteral("N") : initial);
    }
    p.setFont(Theme::uiFont(14, QFont::DemiBold));
    p.setPen(c.starlight);
    const double nameRight = m_collapseRect.left() - 6;
    p.drawText(QRectF(m_headerRect.left() + 36, m_headerRect.top(), nameRight - m_headerRect.left() - 36, m_headerRect.height()),
               Qt::AlignVCenter, QFontMetricsF(p.font()).elidedText(name, Qt::ElideRight, nameRight - m_headerRect.left() - 36));
    for (int k : {201, 202}) {
        const QRect r = k == 201 ? m_collapseRect : m_newRect;
        if (m_hover == k)
            Theme::paintHover(p, r, 1.3, 6);
        if (k == 201 && m_hover < 200 && m_hover != 201 && !underMouse())
            continue;
        Icons::paint(p, QRectF(r).adjusted(5, 5, -5, -5), k == 201 ? QStringLiteral("chevrons-left") : QStringLiteral("pencil"),
                     m_hover == k ? c.starlight : c.haze);
    }

    auto paintItem = [&](const Item &it, bool hover, bool active) {
        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.075));
            p.drawRoundedRect(it.rect, 6, 6);
        } else if (hover) {
            Theme::paintHover(p, it.rect, 0.9, 6);
        }
        Icons::paint(p, QRectF(it.rect.left() + 9, it.rect.center().y() - 8.5, 17, 17), it.icon, active || hover ? c.starlight : c.mist);
        p.setFont(Theme::uiFont(13.5, active ? QFont::Medium : QFont::Normal));
        p.setPen(active || hover ? c.starlight : c.moon);
        p.drawText(QRectF(it.rect.left() + 36, it.rect.top(), it.rect.width() - 40, it.rect.height()), Qt::AlignVCenter, it.text);
        if (!it.hint.isEmpty() && hover) {
            p.setFont(Theme::uiFont(11.5));
            p.setPen(c.dust);
            p.drawText(QRectF(it.rect.left(), it.rect.top(), it.rect.width() - 10, it.rect.height()), Qt::AlignVCenter | Qt::AlignRight,
                       it.hint);
        }
    };
    for (int i = 0; i < m_top.size(); ++i)
        paintItem(m_top[i], m_hover == i, m_top[i].id == QLatin1String("home") && m_homeActive);
    for (int i = 0; i < m_bottom.size(); ++i)
        paintItem(m_bottom[i], m_hover == 100 + i, false);
}

int Sidebar::itemAt(const QPoint &pos) const
{
    if (m_collapseRect.contains(pos))
        return 201;
    if (m_newRect.contains(pos))
        return 202;
    if (m_headerRect.contains(pos))
        return 200;
    for (int i = 0; i < m_top.size(); ++i)
        if (m_top[i].rect.contains(pos))
            return i;
    for (int i = 0; i < m_bottom.size(); ++i)
        if (m_bottom[i].rect.contains(pos))
            return 100 + i;
    return -1;
}

void Sidebar::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    if (m_resizing) {
        emit widthDragged(qBound(200, pos.x(), 480));
        return;
    }
    const int h = itemAt(pos);
    if (h != m_hover) {
        m_hover = h;
        update();
    }
    const bool edge = pos.x() >= width() - 4;
    setCursor(edge ? Qt::SplitHCursor : (h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor));
    setToolTip(h == 201 ? tr("Close sidebar (Ctrl+\\)") : h == 202 ? tr("New page (Ctrl+N)") : QString());
}

void Sidebar::mousePressEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    if (pos.x() >= width() - 4) {
        m_resizing = true;
        return;
    }
    const int h = itemAt(pos);
    if (h == 201)
        emit collapseRequested();
    else if (h == 202)
        emit newPage(QString());
    else if (h == 200)
        emit settingsRequested();
    else if (h >= 0 && h < 100) {
        const QString id = m_top[h].id;
        if (id == QLatin1String("search"))
            emit searchRequested();
        else if (id == QLatin1String("home"))
            emit homeRequested();
        else if (id == QLatin1String("import"))
            emit importRequested(mapToGlobal(m_top[h].rect.topRight() + QPoint(8, 0)));
    } else if (h >= 100) {
        const QString id = m_bottom[h - 100].id;
        if (id == QLatin1String("settings"))
            emit settingsRequested();
        else
            emit trashRequested(mapToGlobal(m_bottom[h - 100].rect.bottomRight() + QPoint(8, 0)));
    }
}

void Sidebar::mouseReleaseEvent(QMouseEvent *)
{
    if (m_resizing) {
        m_resizing = false;
        Settings::instance().save();
    }
}

void Sidebar::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}
