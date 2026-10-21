// The video player (Qt WebEngine) as a plugin the app loads the first time
// a video plays, so starting the app doesn't load a web browser engine.
#include <QDesktopServices>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWebEngineFullScreenRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWidget>

namespace
{
const QUrl &baseUrl()
{
    static const QUrl url(QStringLiteral("https://nebula-note.app/"));
    return url;
}

// Off the record: nothing (cookies, cache) is written to disk.
QWebEngineProfile *playerProfile()
{
    static QWebEngineProfile *p = new QWebEngineProfile(qApp);
    return p;
}

// The page only ever shows the player. Inside the player's own frame
// anything goes; links out of it, and new windows ("Watch on YouTube"),
// open in the browser instead of replacing the player.
class PlayerPage : public QWebEnginePage
{
public:
    explicit PlayerPage(QObject *parent)
        : QWebEnginePage(playerProfile(), parent)
    {
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        if (!isMainFrame || type == NavigationTypeTyped || url.scheme() == QLatin1String("data") || url == baseUrl())
            return true;
        openOutside(url);
        return false;
    }

    QWebEnginePage *createWindow(WebWindowType) override
    {
        auto *opener = new QWebEnginePage(playerProfile(), this);
        connect(opener, &QWebEnginePage::urlChanged, opener, [opener](const QUrl &url) {
            openOutside(url);
            opener->deleteLater();
        });
        return opener;
    }

private:
    static void openOutside(const QUrl &url)
    {
        if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
            QDesktopServices::openUrl(url);
    }
};

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
        m_view->setPage(new PlayerPage(m_view));
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
        m_view->setHtml(html, baseUrl());
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

extern "C" Q_DECL_EXPORT int nebula_webplayer_abi()
{
    return 1;
}

extern "C" Q_DECL_EXPORT QWidget *nebula_webplayer_create(QWidget *parent, const QString &playerUrl)
{
    return new PlayerFrame(parent, playerUrl);
}
