#pragma once

#include <QObject>
#include <QStringList>

class QLocalServer;

// One Nebula per user: a second launch hands its arguments (and the
// compositor's activation token) to the running instance and exits.
class SingleInstance : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);

    // True when another instance received the message; the caller should exit.
    bool forwardToRunning(const QStringList &arguments);
    // Starts listening for later launches.
    bool listen();

signals:
    void messageReceived(const QStringList &arguments, const QString &activationToken);

private:
    QString m_key;
    QLocalServer *m_server = nullptr;
};
