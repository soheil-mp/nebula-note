#pragma once

#include <QByteArray>
#include <QString>

// Downloads a URL and waits for it (follows redirects, gives up after
// `timeoutMs`). The work happens on a helper thread, so the caller's event
// loop doesn't run in between. Returns empty on failure.
QByteArray fetchUrl(const QString &url, QString *mimeType = nullptr, int timeoutMs = 20000);

// Lets imports download remote images into the workspace.
void installImportFetcher();
