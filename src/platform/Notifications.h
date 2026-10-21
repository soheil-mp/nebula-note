#pragma once

#include <QString>

// Desktop notifications (reminders). Talks to the notification service over
// D-Bus, which also works inside a Flatpak; falls back to notify-send.
namespace Notifications
{
void show(const QString &summary, const QString &body);
} // namespace Notifications
