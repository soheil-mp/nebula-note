#include "Peek.h"

#include "PageView.h"
#include "core/Workspace.h"
#include "editor/BlockEditor.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace
{
// The card behind the page: rounded, slate, with a soft shadow.
class PanelFrame : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto &c = Theme::colors();
        p.setPen(QPen(c.ash, 1));
        p.setBrush(c.basalt);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    }
};
} // namespace

Peek::Peek(QWidget *host)
    : QWidget(host)
{
    setObjectName(QStringLiteral("peek"));
    hide();
    m_panel = new PanelFrame(this);
    auto *v = new QVBoxLayout(m_panel);
    v->setContentsMargins(1, 6, 1, 1);
    v->setSpacing(0);
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(10, 0, 10, 0);
    bar->setSpacing(2);
    m_close = new FlatButton(QStringLiteral("chevrons-left"));
    m_close->setToolTip(tr("Close (Esc)"));
    m_full = new FlatButton(QStringLiteral("fullscreen"));
    m_full->setToolTip(tr("Open as full page"));
    m_switch = new FlatButton(QStringLiteral("peek-center"));
    m_switch->setToolTip(tr("Switch peek mode"));
    bar->addWidget(m_close);
    bar->addWidget(m_full);
    bar->addWidget(m_switch);
    bar->addStretch();
    v->addLayout(bar);
    m_view = new PageView(m_panel);
    m_view->setObjectName(QStringLiteral("peekPage"));
    v->addWidget(m_view, 1);
    m_view->editor()->setOverlayHost(host);

    connect(m_close, &FlatButton::clicked, this, &Peek::closePeek);
    connect(m_full, &FlatButton::clicked, this, [this] {
        const QString id = pageId();
        closePeek();
        emit openFull(id);
    });
    connect(m_switch, &FlatButton::clicked, this, [this] { showPage(pageId(), m_mode == QLatin1String("side") ? QStringLiteral("center") : QStringLiteral("side")); });
    connect(m_view, &PageView::openPage, this, [this](const QString &id) {
        closePeek();
        emit openPage(id);
    });
    connect(m_view, &PageView::toast, this, &Peek::toast);
    m_anim.setDuration(Theme::animMs(180));
    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this] { relayout(); });
    host->installEventFilter(this);
    if (Workspace *ws = Workspace::instance())
        connect(ws, &Workspace::treeChanged, this, [this] {
            const PageMeta *m = Workspace::instance()->page(pageId());
            if (isVisible() && (!m || m->isTrashed()))
                closePeek();
        });
}

QString Peek::pageId() const
{
    return m_view->pageId();
}

void Peek::setArea(const QRect &area)
{
    m_area = area;
    if (isVisible())
        relayout();
}

QRect Peek::panelRect() const
{
    const QRect host = parentWidget()->rect();
    if (m_mode == QLatin1String("center")) {
        const int w = qMin(960, int(host.width() * 0.82));
        const int h = int(host.height() * 0.88);
        return QRect(host.center().x() - w / 2, host.center().y() - h / 2 + 8, w, h);
    }
    const QRect area = m_area.isValid() ? m_area : host;
    const int w = qBound(480, int(area.width() * 0.48), 760);
    return QRect(area.right() - w - 6, area.top() + 6, w, area.height() - 12);
}

void Peek::relayout()
{
    const QRect host = parentWidget()->rect();
    const double t = m_anim.state() == QAbstractAnimation::Running ? m_anim.currentValue().toDouble() : 1.0;
    QRect r = panelRect();
    if (m_mode == QLatin1String("center")) {
        setGeometry(host);
        m_panel->setGeometry(r.translated(0, int((1 - t) * 24)));
    } else {
        // The side peek only covers its own panel.
        r.translate(int((1 - t) * 60), 0);
        setGeometry(r.adjusted(-14, -6, 6, 6));
        m_panel->setGeometry(QRect(QPoint(14, 6), r.size()));
    }
}

void Peek::showPage(const QString &pageId, const QString &mode)
{
    Workspace *ws = Workspace::instance();
    if (!ws || !ws->exists(pageId))
        return;
    const bool wasOpen = isVisible();
    const bool modeChanged = mode != m_mode;
    m_mode = mode == QLatin1String("center") ? QStringLiteral("center") : QStringLiteral("side");
    m_switch->setIconName(m_mode == QLatin1String("side") ? QStringLiteral("peek-center") : QStringLiteral("peek-side"));
    m_close->setIconName(m_mode == QLatin1String("side") ? QStringLiteral("chevrons-left") : QStringLiteral("close"));
    if (m_view->pageId() != pageId) {
        m_view->flush();
        m_view->setPage(pageId);
    }
    show();
    raise();
    if (!wasOpen || modeChanged) {
        m_anim.stop();
        m_anim.setStartValue(0.0);
        m_anim.setEndValue(1.0);
        m_anim.start();
    }
    relayout();
    m_view->focusDefault();
}

void Peek::closePeek()
{
    if (!isVisible())
        return;
    m_view->flush();
    hide();
}

void Peek::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_mode == QLatin1String("center")) {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), Theme::kRadius, Theme::kRadius);
        p.fillPath(clip, QColor(0, 0, 0, 120));
        Theme::paintSoftShadow(p, m_panel->geometry(), 12, 26, 1.6);
    } else {
        Theme::paintSoftShadow(p, m_panel->geometry(), 12, 14, 1.2);
    }
}

void Peek::mousePressEvent(QMouseEvent *e)
{
    // Clicks on the dimmed window close a center peek.
    if (m_mode == QLatin1String("center") && !m_panel->geometry().contains(e->position().toPoint()))
        closePeek();
}

void Peek::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        closePeek();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool Peek::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
        relayout();
    return QWidget::eventFilter(watched, event);
}
