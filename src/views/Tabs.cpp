#include "Tabs.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>

TabStrip::TabStrip(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(36);
    setObjectName(QStringLiteral("tabStrip"));
}

void TabStrip::setTabs(const QList<Tab> &tabs, int current)
{
    m_tabs = tabs;
    m_current = current;
    layoutTabs();
    update();
}

void TabStrip::layoutTabs()
{
    m_rects.clear();
    const int n = int(m_tabs.size());
    const int avail = width() - 16 - 36;
    const int w = n > 0 ? qBound(90, avail / qMax(1, n), 230) : 0;
    int x = 8;
    for (int i = 0; i < n; ++i) {
        m_rects.append(QRect(x, 4, w - 4, height() - 8));
        x += w;
    }
    m_addRect = QRect(x + 2, 6, 24, 24);
}

QRect TabStrip::tabRect(int index) const
{
    return m_rects.value(index);
}

QRect TabStrip::closeRect(int index) const
{
    const QRect r = m_rects.value(index);
    return QRect(r.right() - 24, r.center().y() - 10, 20, 20);
}

void TabStrip::resizeEvent(QResizeEvent *)
{
    layoutTabs();
}

void TabStrip::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c.hairline, 1));
    p.drawLine(QPointF(0, height() - 0.5), QPointF(width(), height() - 0.5));
    for (int i = 0; i < m_tabs.size(); ++i) {
        const QRect r = m_rects[i];
        const bool current = i == m_current;
        const bool hover = i == m_hover;
        if (current) {
            p.setPen(QPen(c.ash, 1));
            p.setBrush(c.graphite);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        } else if (hover) {
            Theme::paintHover(p, r, 0.9, 7);
        }
        Icons::paintPageIcon(p, QRectF(r.left() + 9, r.center().y() - 8, 16, 16), m_tabs[i].icon, current ? c.starlight : c.mist,
                             QStringLiteral("page"));
        const int right = (hover || current) ? closeRect(i).left() - 4 : r.right() - 8;
        p.setFont(Theme::uiFont(12.5, current ? QFont::Medium : QFont::Normal));
        p.setPen(current ? c.starlight : c.haze);
        const QRectF tr(r.left() + 32, r.top(), right - r.left() - 32, r.height());
        p.drawText(tr, Qt::AlignVCenter, QFontMetricsF(p.font()).elidedText(m_tabs[i].title, Qt::ElideRight, tr.width()));
        if (hover || current) {
            const QRect cr = closeRect(i);
            if (hover && m_hoverClose)
                Theme::paintHover(p, cr, 1.3, 5);
            Icons::paint(p, QRectF(cr).adjusted(4, 4, -4, -4), QStringLiteral("close"), hover && m_hoverClose ? c.starlight : c.fog);
        }
    }
    if (m_hoverAdd)
        Theme::paintHover(p, m_addRect, 1.2, 6);
    Icons::paint(p, QRectF(m_addRect).adjusted(5, 5, -5, -5), QStringLiteral("plus"), m_hoverAdd ? c.starlight : c.fog);
}

void TabStrip::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    int hover = -1;
    for (int i = 0; i < m_rects.size(); ++i)
        if (m_rects[i].contains(pos))
            hover = i;
    const bool close = hover >= 0 && closeRect(hover).contains(pos);
    const bool add = m_addRect.contains(pos);
    if (hover != m_hover || close != m_hoverClose || add != m_hoverAdd) {
        m_hover = hover;
        m_hoverClose = close;
        m_hoverAdd = add;
        update();
    }
    setCursor(hover >= 0 || add ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setToolTip(hover >= 0 ? m_tabs[hover].title : add ? tr("New tab (Ctrl+T)") : QString());
}

void TabStrip::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::MiddleButton && m_hover >= 0) {
        emit closeRequested(m_hover);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (m_hoverAdd)
        emit newTabRequested();
    else if (m_hover >= 0 && m_hoverClose)
        emit closeRequested(m_hover);
    else if (m_hover >= 0)
        emit activated(m_hover);
}

void TabStrip::leaveEvent(QEvent *)
{
    m_hover = -1;
    m_hoverClose = m_hoverAdd = false;
    update();
}
