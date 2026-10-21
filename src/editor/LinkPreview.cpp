#include "LinkPreview.h"

#include "core/MediaLinks.h"
#include "core/Workspace.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QUrl>

LinkPreview::LinkPreview(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
    m_net->setTransferTimeout(10000);
}

LinkPreview *LinkPreview::instance()
{
    static LinkPreview *p = new LinkPreview(qApp);
    return p;
}

namespace
{
QString decode(const QString &s)
{
    return QTextDocumentFragment::fromHtml(s).toPlainText().simplified();
}

QString metaContent(const QString &html, const QString &key)
{
    // <meta property="og:title" content="…"> in either attribute order.
    const QString k = QRegularExpression::escape(key);
    const QRegularExpression a(QStringLiteral("<meta[^>]+(?:property|name)\\s*=\\s*[\"']%1[\"'][^>]*content\\s*=\\s*[\"']([^\"']*)[\"']").arg(k),
                               QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression b(QStringLiteral("<meta[^>]+content\\s*=\\s*[\"']([^\"']*)[\"'][^>]*(?:property|name)\\s*=\\s*[\"']%1[\"']").arg(k),
                               QRegularExpression::CaseInsensitiveOption);
    if (auto m = a.match(html); m.hasMatch())
        return decode(m.captured(1));
    if (auto m = b.match(html); m.hasMatch())
        return decode(m.captured(1));
    return {};
}
} // namespace

LinkPreview::Info LinkPreview::parse(const QByteArray &bytes, const QString &baseUrl)
{
    Info info;
    const QString html = QString::fromUtf8(bytes.left(400000));
    info.title = metaContent(html, QStringLiteral("og:title"));
    if (info.title.isEmpty())
        info.title = metaContent(html, QStringLiteral("twitter:title"));
    if (info.title.isEmpty()) {
        static const QRegularExpression title(QStringLiteral("<title[^>]*>([\\s\\S]*?)</title>"), QRegularExpression::CaseInsensitiveOption);
        if (auto m = title.match(html); m.hasMatch())
            info.title = decode(m.captured(1));
    }
    info.description = metaContent(html, QStringLiteral("og:description"));
    if (info.description.isEmpty())
        info.description = metaContent(html, QStringLiteral("description"));
    info.site = metaContent(html, QStringLiteral("og:site_name"));
    QString image = metaContent(html, QStringLiteral("og:image"));
    if (image.isEmpty())
        image = metaContent(html, QStringLiteral("twitter:image"));
    if (!image.isEmpty())
        info.imageUrl = QUrl(baseUrl).resolved(QUrl(image)).toString();
    info.ok = !info.title.isEmpty() || !info.description.isEmpty();
    return info;
}

QString LinkPreview::providerOf(const QString &url)
{
    const QString host = QUrl(url).host().toLower();
    static const QList<QPair<const char *, const char *>> known = {
        {"youtube.com", "YouTube"}, {"youtu.be", "YouTube"},       {"vimeo.com", "Vimeo"},     {"figma.com", "Figma"},
        {"gist.github.com", "GitHub Gist"}, {"github.com", "GitHub"}, {"maps.google", "Google Maps"}, {"google.com/maps", "Google Maps"},
        {"docs.google.com", "Google Docs"}, {"drive.google.com", "Google Drive"}, {"twitter.com", "X"}, {"x.com", "X"},
        {"codepen.io", "CodePen"}, {"loom.com", "Loom"},           {"miro.com", "Miro"},       {"spotify.com", "Spotify"},
        {"soundcloud.com", "SoundCloud"}, {"twitch.tv", "Twitch"}, {"tiktok.com", "TikTok"},  {"instagram.com", "Instagram"},
        {"pdf", "PDF"},
    };
    const QString full = host + QUrl(url).path();
    for (const auto &k : known)
        if (full.contains(QLatin1String(k.first)))
            return QString::fromLatin1(k.second);
    return host.startsWith(QLatin1String("www.")) ? host.mid(4) : host;
}

void LinkPreview::fetch(const QString &url, std::function<void(const Info &)> done)
{
    // Videos: the provider's oEmbed answers with the title and a thumbnail
    // (the watch page itself may be a consent wall).
    const QString oembed = MediaLinks::oembedUrl(url);
    QNetworkRequest req{QUrl(oembed.isEmpty() ? url : oembed)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) NebulaNote/1.0"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, oembed, done] {
        reply->deleteLater();
        Info info;
        if (reply->error() == QNetworkReply::NoError && !oembed.isEmpty()) {
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            info.title = o.value(QStringLiteral("title")).toString();
            info.site = o.value(QStringLiteral("author_name")).toString();
            info.imageUrl = o.value(QStringLiteral("thumbnail_url")).toString();
            info.ok = !info.title.isEmpty();
        } else if (reply->error() == QNetworkReply::NoError) {
            info = parse(reply->readAll(), reply->url().toString());
        }
        if (info.imageUrl.isEmpty())
            info.imageUrl = MediaLinks::thumbnailUrl(url);
        if (info.imageUrl.isEmpty() || !Workspace::instance()) {
            done(info);
            return;
        }
        // The preview image goes into the media folder (cards stay offline-friendly).
        QNetworkReply *img = m_net->get(QNetworkRequest(QUrl(info.imageUrl)));
        connect(img, &QNetworkReply::finished, this, [img, info, done]() mutable {
            img->deleteLater();
            if (img->error() == QNetworkReply::NoError && Workspace::instance()) {
                const QByteArray data = img->readAll();
                if (data.size() < 8 * 1024 * 1024) {
                    QString suffix = QUrl(info.imageUrl).fileName().section(QLatin1Char('.'), -1).toLower();
                    if (suffix.size() > 4 || suffix.isEmpty())
                        suffix = QStringLiteral("jpg");
                    info.imageMedia = Workspace::instance()->importMediaData(data, suffix);
                }
            }
            done(info);
        });
    });
}
