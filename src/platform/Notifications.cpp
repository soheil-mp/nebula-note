#include "Notifications.h"

#include <QProcess>
#include <QStandardPaths>
#include <QVariantMap>

#ifdef NEBULA_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#endif

namespace Notifications
{
void show(const QString &summary, const QString &body)
{
    if (qEnvironmentVariableIsSet("NEBULA_NO_NOTIFY"))
        return;
#ifdef NEBULA_HAVE_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                                           QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
        call << QStringLiteral("Nebula Note") << uint(0) << QStringLiteral(NEBULA_APP_ID) << summary << body << QStringList()
             << QVariantMap{{QStringLiteral("desktop-entry"), QStringLiteral(NEBULA_APP_ID)}} << int(-1);
        if (bus.send(call))
            return;
    }
#endif
    const QString notifier = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
    if (!notifier.isEmpty())
        QProcess::startDetached(notifier, {QStringLiteral("--app-name=Nebula Note"), QStringLiteral("--icon=" NEBULA_APP_ID), summary, body});
}
} // namespace Notifications
