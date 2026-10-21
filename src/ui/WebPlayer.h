#pragma once

#include <QString>

class QWidget;

// Plays an embedded web player (YouTube, Vimeo) inside the page. The player
// is a plugin (Qt WebEngine) loaded the first time a video plays; without
// it, videos open in the browser instead.
namespace WebPlayer
{
bool available(); // the plugin is installed (doesn't load it)
// A player widget for an embed URL (nullptr when it can't be loaded).
QWidget *create(QWidget *parent, const QString &playerUrl);
} // namespace WebPlayer
