#include "TopBar.h"

#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

TopBar::TopBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(46);
    m_tick.setInterval(30000);
    connect(&m_tick, &QTimer::timeout, this, &TopBar::relayout);
    m_tick.start();
    if (Workspace *ws = Workspace::instance()) {
        connect(ws, &Workspace::pageChanged, this, &TopBar::relayout);
        connect(ws, &Workspace::treeChanged, this, &TopBar::relayout);
        connect(ws, &Workspace::favoritesChanged, this, &TopBar::relayout);
        connect(ws, &Workspace::contentChanged, this, [this](const QString &id, QObject *) {
            if (id == m_pageId)
                relayout();
        });
    }
}

void TopBar::setPage(const QString &pageId)
{
    m_pageId = pageId;
    relayout();
}

void TopBar::setNavigation(bool canBack, bool canForward)
{
    m_canBack = canBack;
    m_canForward = canForward;
    update();
}

void TopBar::setSidebarCollapsed(bool collapsed)
{
    m_sidebarCollapsed = collapsed;
    relayout();
}

void TopBar::setMaximized(bool maximized)
{
    m_maximized = maximized;
    update();
}

void TopBar::resizeEvent(QResizeEvent *)
{
    relayout();
}

void TopBar::relayout()
{
    m_hits.clear();
    m_crumbs.clear();
    Workspace *ws = Workspace::instance();
    const double cy = height() / 2.0;
    double x = 10;
    if (m_sidebarCollapsed) {
        m_hits.append({QStringLiteral("sidebar"), QRectF(x, cy - 14, 28, 28), tr("Open sidebar (Ctrl+\\)")});
        x += 32;
    }
    m_hits.append({QStringLiteral("back"), QRectF(x, cy - 14, 28, 28), tr("Back (Ctrl+[)")});
    x += 30;
    m_hits.append({QStringLiteral("forward"), QRectF(x, cy - 14, 28, 28), tr("Forward (Ctrl+])")});
    x += 38;

    // Right side, from the window edge inwards.
    double r = width() - 8;
    for (const QString &id : {QStringLiteral("close"), QStringLiteral("max"), QStringLiteral("min")}) {
        r -= 34;
        m_hits.append({id, QRectF(r, cy - 15, 30, 30), id == QLatin1String("close") ? tr("Close") : id == QLatin1String("max") ? tr("Maximize") : tr("Minimize")});
    }
    r -= 14;
    const PageMeta *m = ws ? ws->page(m_pageId) : nullptr;
    m_edited.clear();
    if (m) {
        r -= 30;
        m_hits.append({QStringLiteral("more"), QRectF(r, cy - 14, 28, 28), tr("Style, export, and more")});
        r -= 32;
        m_hits.append({QStringLiteral("star"), QRectF(r, cy - 14, 28, 28), m->favorite > 0 ? tr("Remove from Favorites") : tr("Add to Favorites")});
        m_edited = tr("Edited %1").arg(relativeTime(m->updated));
        const double w = QFontMetricsF(Theme::uiFont(12.5)).horizontalAdvance(m_edited);
        r -= w + 12;
        m_editedRect = QRectF(r, 0, w, height());
        // Locked pages say so, and a click unlocks them.
        if (ws->options(m_pageId).locked) {
            const double lw = QFontMetricsF(Theme::uiFont(12.5, QFont::Medium)).horizontalAdvance(tr("Locked")) + 34;
            r -= lw + 10;
            m_hits.append({QStringLiteral("unlock"), QRectF(r, cy - 13, lw, 26), tr("Click to unlock the page")});
        }
    }

    // Breadcrumbs between.
    double maxRight = r - 12;
    QList<Crumb> all;
    if (m) {
        for (const QString &a : ws->ancestors(m_pageId)) {
            const PageMeta *am = ws->page(a);
            all.append({a, ws->title(a), am ? am->icon : QString(), {}, false});
        }
        all.append({m_pageId, ws->title(m_pageId), m->icon, {}, false});
    } else {
        all.append({QString(), tr("Home"), QStringLiteral("glyph:home"), {}, false});
    }
    // Measure in the heavier weight used for the current page.
    const QFont f = Theme::uiFont(13.5, QFont::Medium);
    const QFontMetricsF fm(f);
    // Pages without an icon still show the default page glyph.
    auto widthOf = [&](const Crumb &c) {
        const bool icon = !c.icon.isEmpty() || (!c.ellipsis && !c.id.isEmpty());
        return c.ellipsis ? 26.0 : qMin(220.0, fm.horizontalAdvance(c.text)) + (icon ? 40 : 16);
    };
    // Collapse the middle until it fits.
    QList<Crumb> shown = all;
    auto total = [&] {
        double t = 0;
        for (const Crumb &c : shown)
            t += widthOf(c) + 14;
        return t;
    };
    while (shown.size() > 2 && total() > maxRight - x) {
        if (shown.size() > 2 && !shown[1].ellipsis)
            shown.insert(1, Crumb{QString(), QStringLiteral("…"), QString(), {}, true});
        if (shown.size() > 3)
            shown.removeAt(2);
        else
            break;
    }
    // Still too tight: the "Edited" note goes first.
    if (total() > maxRight - x && !m_edited.isEmpty()) {
        const double freed = m_editedRect.width() + 12;
        for (Hit &h : m_hits)
            if (h.id == QLatin1String("unlock"))
                h.rect.translate(freed, 0); // the Locked chip slides over
        maxRight += freed;
        m_edited.clear();
    }
    // Then the current page keeps its full title as long as it can, and the
    // crumbs before it share what's left.
    QList<double> widths;
    for (const Crumb &c : shown)
        widths.append(widthOf(c));
    const double room = maxRight - x - 14 * shown.size();
    double sum = 0;
    for (double w : widths)
        sum += w;
    if (sum > room && !widths.isEmpty()) {
        const int n = int(widths.size());
        double others = 0;
        for (int i = 0; i < n - 1; ++i)
            others += shown[i].ellipsis ? widths[i] : 40;
        widths.last() = qMax(40.0, qMin(widths.last(), room - others));
        // Earlier crumbs split the remainder in proportion to their wants.
        double left = room - widths.last();
        double want = 0;
        for (int i = 0; i < n - 1; ++i)
            if (shown[i].ellipsis)
                left -= widths[i];
        for (int i = 0; i < n - 1; ++i)
            if (!shown[i].ellipsis)
                want += widths[i];
        for (int i = 0; i < n - 1; ++i)
            if (!shown[i].ellipsis && want > 0)
                widths[i] = qMax(40.0, qMin(widths[i], left * widths[i] / want));
    }
    for (int i = 0; i < shown.size(); ++i) {
        Crumb &c = shown[i];
        c.rect = QRectF(x, cy - 14, widths[i], 28);
        x += widths[i] + 14;
        m_crumbs.append(c);
        if (!c.ellipsis)
            m_hits.append({QStringLiteral("crumb:") + c.id, c.rect, QString()});
    }
    update();
}

int TopBar::hitAt(const QPointF &pos) const
{
    for (int i = 0; i < m_hits.size(); ++i)
        if (m_hits[i].rect.contains(pos))
            return i;
    return -1;
}

void TopBar::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Workspace *ws = Workspace::instance();
    const PageMeta *m = ws ? ws->page(m_pageId) : nullptr;
    for (int i = 0; i < m_hits.size(); ++i) {
        const Hit &h = m_hits[i];
        const bool hover = i == m_hover;
        if (h.id.startsWith(QLatin1String("crumb:")))
            continue;
        if (h.id == QLatin1String("unlock")) {
            // A quiet chip: lock glyph and "Locked".
            p.setPen(QPen(hover ? c.smoke : c.ash, 1));
            p.setBrush(hover ? QBrush(c.graphite) : QBrush(Qt::NoBrush));
            p.drawRoundedRect(h.rect.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
            Icons::paint(p, QRectF(h.rect.left() + 9, h.rect.center().y() - 7, 14, 14), QStringLiteral("lock"), hover ? c.starlight : c.mist);
            p.setFont(Theme::uiFont(12.5, QFont::Medium));
            p.setPen(hover ? c.starlight : c.mist);
            p.drawText(h.rect.adjusted(27, 0, -6, 0), Qt::AlignVCenter, tr("Locked"));
            continue;
        }
        QString icon;
        QColor color = hover ? c.starlight : c.haze;
        bool enabled = true;
        if (h.id == QLatin1String("sidebar"))
            icon = QStringLiteral("sidebar");
        else if (h.id == QLatin1String("back")) {
            icon = QStringLiteral("arrow-left");
            enabled = m_canBack;
        } else if (h.id == QLatin1String("forward")) {
            icon = QStringLiteral("arrow-right");
            enabled = m_canForward;
        } else if (h.id == QLatin1String("more"))
            icon = QStringLiteral("more");
        else if (h.id == QLatin1String("star")) {
            const bool fav = m && m->favorite > 0;
            icon = fav ? QStringLiteral("star-filled") : QStringLiteral("star");
            if (fav)
                color = c.starlight;
        } else if (h.id == QLatin1String("min"))
            icon = QStringLiteral("win-min");
        else if (h.id == QLatin1String("max"))
            icon = m_maximized ? QStringLiteral("win-restore") : QStringLiteral("win-max");
        else if (h.id == QLatin1String("close"))
            icon = QStringLiteral("close");
        if (!enabled)
            color = c.dust;
        if (hover && enabled) {
            if (h.id == QLatin1String("close")) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0xC9, 0xC9, 0xD0, 220));
                p.drawEllipse(h.rect.center(), 12, 12);
                color = c.basalt;
            } else {
                Theme::paintHover(p, h.rect, 1.2, h.id == QLatin1String("min") || h.id == QLatin1String("max") ? 12 : 6);
            }
        }
        const double s = h.id == QLatin1String("close") || h.id == QLatin1String("min") || h.id == QLatin1String("max") ? 16 : 17;
        Icons::paint(p, QRectF(h.rect.center().x() - s / 2, h.rect.center().y() - s / 2, s, s), icon, color);
    }
    // Breadcrumbs.
    for (int i = 0; i < m_crumbs.size(); ++i) {
        const Crumb &cr = m_crumbs[i];
        if (i > 0) {
            p.setPen(c.dust);
            p.setFont(Theme::uiFont(13));
            p.drawText(QRectF(cr.rect.left() - 13, cr.rect.top(), 12, cr.rect.height()), Qt::AlignCenter, QStringLiteral("/"));
        }
        bool hover = false;
        for (int k = 0; k < m_hits.size(); ++k)
            if (k == m_hover && m_hits[k].id == QStringLiteral("crumb:") + cr.id && !cr.ellipsis)
                hover = true;
        if (hover)
            Theme::paintHover(p, cr.rect, 1.0, 6);
        const bool last = i == m_crumbs.size() - 1;
        double x = cr.rect.left() + 8;
        if (!cr.icon.isEmpty() || (!cr.ellipsis && !cr.id.isEmpty())) {
            const PageMeta *cm = ws ? ws->page(cr.id) : nullptr;
            Icons::paintPageIcon(p, QRectF(x, cr.rect.center().y() - 9, 18, 18), cr.icon, last ? c.starlight : c.mist,
                                 cm && cm->isDatabase() ? QStringLiteral("database") : QStringLiteral("page"));
            x += 24;
        }
        p.setFont(Theme::uiFont(13.5, last ? QFont::Medium : QFont::Normal));
        p.setPen(last ? c.starlight : c.haze);
        p.drawText(QRectF(x, cr.rect.top(), cr.rect.right() - x - 6, cr.rect.height()), Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(cr.text, Qt::ElideRight, cr.rect.right() - x - 6));
    }
    if (!m_edited.isEmpty()) {
        p.setFont(Theme::uiFont(12.5));
        p.setPen(c.fog);
        p.drawText(m_editedRect, Qt::AlignVCenter | Qt::AlignRight, m_edited);
    }
}

void TopBar::mouseMoveEvent(QMouseEvent *e)
{
    const int h = hitAt(e->position());
    if (h != m_hover) {
        m_hover = h;
        setToolTip(h >= 0 ? m_hits[h].tooltip : QString());
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void TopBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int h = hitAt(e->position());
    if (h < 0) {
        // Empty space drags the window.
        if (QWindow *w = window()->windowHandle())
            w->startSystemMove();
        return;
    }
    const QString id = m_hits[h].id;
    if (id == QLatin1String("sidebar"))
        emit toggleSidebar();
    else if (id == QLatin1String("back") && m_canBack)
        emit back();
    else if (id == QLatin1String("forward") && m_canForward)
        emit forward();
    else if (id == QLatin1String("star"))
        emit favoriteToggled();
    else if (id == QLatin1String("more"))
        emit pageMenuRequested(mapToGlobal(QPoint(int(m_hits[h].rect.right()), int(m_hits[h].rect.bottom()) + 6)));
    else if (id == QLatin1String("unlock")) {
        PageOptions o = Workspace::instance()->options(m_pageId);
        o.locked = false;
        Workspace::instance()->setOptions(m_pageId, o);
    }
    else if (id == QLatin1String("min"))
        emit minimizeRequested();
    else if (id == QLatin1String("max"))
        emit maximizeRequested();
    else if (id == QLatin1String("close"))
        emit closeRequested();
    else if (id.startsWith(QLatin1String("crumb:"))) {
        const QString page = id.mid(6);
        if (page == m_pageId)
            emit currentClicked();
        else
            emit navigate(page);
    }
}

void TopBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (hitAt(e->position()) < 0)
        emit maximizeRequested();
}

void TopBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}
