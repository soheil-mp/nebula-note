#include "WebPlayer.h"

#ifdef NEBULA_HAVE_WEBENGINE
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWebEngineFullScreenRequest>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWidget>

namespace
{
// The player inside a frame with the page's rounded corners.
class PlayerFrame : public QWidget
{
public:
    PlayerFrame(QWidget *parent, const QString &url)
        : QWidget(parent)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        m_view = new QWebEngineView(this);
        QWebEngineSettings *s = m_view->settings();
        s->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
        s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
        m_view->page()->setBackgroundColor(QColor(0x0C, 0x0C, 0x0F));
        // An iframe in a small page, so the player sees a referring page as
        // it does on the web (players refuse to start without one).
        const QString html = QStringLiteral(
                                 "<!doctype html><html><head><meta charset='utf-8'><style>html,body{margin:0;height:100%;background:#0c0c0f;overflow:hidden}"
                                 "iframe{border:0;width:100%;height:100%}</style></head><body><iframe src='%1' allow='autoplay; encrypted-media; "
                                 "picture-in-picture; fullscreen' allowfullscreen></iframe></body></html>")
                                 .arg(url.toHtmlEscaped());
        m_view->setHtml(html, QUrl(QStringLiteral("https://nebula-note.app/")));
        connect(m_view->page(), &QWebEnginePage::fullScreenRequested, this, [](QWebEngineFullScreenRequest request) { request.accept(); });
        v->addWidget(m_view);
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), 10, 10);
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

private:
    QWebEngineView *m_view;
};
} // namespace

bool WebPlayer::available()
{
    return true;
}

QWidget *WebPlayer::create(QWidget *parent, const QString &playerUrl)
{
    return new PlayerFrame(parent, playerUrl);
}
#else
bool WebPlayer::available()
{
    return false;
}

QWidget *WebPlayer::create(QWidget *, const QString &)
{
    return nullptr;
}
#endif
