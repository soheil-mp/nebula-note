#include "MediaLinks.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace MediaLinks
{
namespace
{
bool youtubeHost(const QString &host)
{
    return host == QLatin1String("youtu.be") || host == QLatin1String("youtube.com") || host.endsWith(QLatin1String(".youtube.com"))
           || host == QLatin1String("youtube-nocookie.com") || host.endsWith(QLatin1String(".youtube-nocookie.com"));
}

QString vimeoId(const QUrl &u)
{
    const QString host = u.host().toLower();
    if (host != QLatin1String("vimeo.com") && !host.endsWith(QLatin1String(".vimeo.com")))
        return {};
    static const QRegularExpression id(QStringLiteral("^/(?:video/)?(\\d{5,12})(?:/|$)"));
    const auto m = id.match(u.path());
    return m.hasMatch() ? m.captured(1) : QString();
}

// "1m30s", "90", "90s" → seconds.
int seconds(const QString &t)
{
    if (t.isEmpty())
        return 0;
    bool ok = false;
    const int plain = t.toInt(&ok);
    if (ok)
        return plain;
    static const QRegularExpression parts(QStringLiteral("(\\d+)([hms])"));
    int total = 0;
    for (auto it = parts.globalMatch(t); it.hasNext();) {
        const auto m = it.next();
        const int n = m.captured(1).toInt();
        total += m.captured(2) == QLatin1String("h") ? n * 3600 : m.captured(2) == QLatin1String("m") ? n * 60 : n;
    }
    return total;
}
} // namespace

QString youtubeId(const QString &url)
{
    const QUrl u(url.trimmed());
    const QString host = u.host().toLower();
    if (!youtubeHost(host))
        return {};
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    QString id;
    if (host == QLatin1String("youtu.be"))
        id = u.path().section(QLatin1Char('/'), 1, 1);
    else if (u.path() == QLatin1String("/watch"))
        id = QUrlQuery(u).queryItemValue(QStringLiteral("v"));
    else {
        static const QRegularExpression path(QStringLiteral("^/(?:shorts|live|embed|v)/([A-Za-z0-9_-]{11})"));
        const auto m = path.match(u.path());
        if (m.hasMatch())
            id = m.captured(1);
    }
    return valid.match(id).hasMatch() ? id : QString();
}

bool isVideoLink(const QString &url)
{
    return !youtubeId(url).isEmpty() || !vimeoId(QUrl(url.trimmed())).isEmpty();
}

QString playerUrl(const QString &url, bool autoplay)
{
    const QUrl u(url.trimmed());
    if (const QString id = youtubeId(url); !id.isEmpty()) {
        QUrlQuery q(u);
        const int start = seconds(q.hasQueryItem(QStringLiteral("t")) ? q.queryItemValue(QStringLiteral("t")) : q.queryItemValue(QStringLiteral("start")));
        QString player = QStringLiteral("https://www.youtube-nocookie.com/embed/%1?rel=0&modestbranding=1&playsinline=1").arg(id);
        if (autoplay)
            player += QStringLiteral("&autoplay=1");
        if (start > 0)
            player += QStringLiteral("&start=%1").arg(start);
        return player;
    }
    if (const QString id = vimeoId(u); !id.isEmpty())
        return QStringLiteral("https://player.vimeo.com/video/%1?dnt=1%2").arg(id, autoplay ? QStringLiteral("&autoplay=1") : QString());
    return {};
}

QString oembedUrl(const QString &url)
{
    if (!youtubeId(url).isEmpty())
        return QStringLiteral("https://www.youtube.com/oembed?format=json&url=") + QString::fromLatin1(QUrl::toPercentEncoding(url.trimmed()));
    if (!vimeoId(QUrl(url.trimmed())).isEmpty())
        return QStringLiteral("https://vimeo.com/api/oembed.json?url=") + QString::fromLatin1(QUrl::toPercentEncoding(url.trimmed()));
    return {};
}

QString thumbnailUrl(const QString &url)
{
    const QString id = youtubeId(url);
    return id.isEmpty() ? QString() : QStringLiteral("https://i.ytimg.com/vi/%1/hqdefault.jpg").arg(id);
}
} // namespace MediaLinks
