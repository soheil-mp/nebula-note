#pragma once

#include <QString>

class QWidget;

// Plays an embedded web player (YouTube, Vimeo) inside the page. Needs Qt
// WebEngine at build time; without it, videos open in the browser instead.
namespace WebPlayer
{
bool available();
// A player widget for an embed URL (nullptr when unavailable).
QWidget *create(QWidget *parent, const QString &playerUrl);
} // namespace WebPlayer
