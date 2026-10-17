#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;

// Fetches a page's title, description and preview image for bookmark cards
// (Open Graph tags, falling back to <title> and <meta name="description">).
class LinkPreview : public QObject
{
    Q_OBJECT
public:
    struct Info {
        QString title;
        QString description;
        QString site;
        QString imageUrl;
        QString imageMedia; // the downloaded preview image, in the media folder
        bool ok = false;
    };
    static LinkPreview *instance();
    void fetch(const QString &url, std::function<void(const Info &)> done);

    // Parses the interesting bits out of an HTML document (public for tests).
    static Info parse(const QByteArray &html, const QString &baseUrl);
    // "YouTube", "Figma", "GitHub Gist"… for embeds.
    static QString providerOf(const QString &url);

private:
    explicit LinkPreview(QObject *parent = nullptr);
    QNetworkAccessManager *m_net;
};
