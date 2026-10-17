#pragma once

#include <QRegion>
#include <QString>

class QWidget;

// Thin wrapper over KWindowSystem (KWin shadows, blur, activation) with
// no-op fallbacks when the framework or compositor support is missing.
namespace WindowEffects
{
bool blurAvailable();
void setBlurBehind(QWidget *window, bool enable, const QRegion &region = QRegion());

// A soft compositor-side drop shadow that follows a rounded frameless
// window. Survives the window being hidden and shown again.
void attachShadow(QWidget *window, int cornerRadius);
void setShadowEnabled(QWidget *window, bool enabled);

// Raises and focuses a window, using an XDG activation token on Wayland.
void activate(QWidget *window, const QString &activationToken = {});
}
