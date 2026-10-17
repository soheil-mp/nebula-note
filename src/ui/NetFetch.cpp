#include "NetFetch.h"

#include "core/Importer.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

#include <thread>

QByteArray fetchUrl(const QString &url, QString *mimeType, int timeoutMs)
{
    QByteArray data;
    QString mime;
    std::thread worker([&] {
        QNetworkAccessManager manager;
        QNetworkRequest request{QUrl(url)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) NebulaNote/1.0"));
        request.setTransferTimeout(timeoutMs);
        QNetworkReply *reply = manager.get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(timeoutMs + 1000, &loop, &QEventLoop::quit);
        loop.exec();
        if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
            data = reply->readAll();
            mime = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        }
        reply->abort();
        delete reply;
    });
    worker.join();
    if (mimeType)
        *mimeType = mime;
    return data;
}

void installImportFetcher()
{
    Importer::setFetcher([](const QString &url, QString *mime) { return fetchUrl(url, mime); });
}
