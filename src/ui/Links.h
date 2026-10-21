#pragma once

#include <QUrl>

class QWidget;

// Opening links from page content. Web, mail and phone links open right
// away; anything else (a file on this computer, another app's scheme) could
// run something, so it asks first. Links without a scheme count as web
// links ("example.com").
namespace Links
{
QUrl normalized(const QString &link);
bool opensDirectly(const QUrl &url);
void open(QWidget *parent, const QString &link);
// A file the workspace keeps (images, attachments): opened as it is.
void openMedia(const QString &path);
} // namespace Links
