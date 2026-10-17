#include "SingleInstance.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
{
}

bool SingleInstance::forwardToRunning(const QStringList &arguments)
{
    QLocalSocket socket;
    socket.connectToServer(m_key);
    if (!socket.waitForConnected(400))
        return false;
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << arguments << QString::fromLocal8Bit(qgetenv("XDG_ACTIVATION_TOKEN"));
    socket.write(payload);
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

bool SingleInstance::listen()
{
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_key)) {
        // A stale socket from a crash: remove it and try again.
        QLocalServer::removeServer(m_key);
        if (!m_server->listen(m_key))
            return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                QDataStream in(socket->readAll());
                QStringList args;
                QString token;
                in >> args >> token;
                emit messageReceived(args, token);
            });
        }
    });
    return true;
}
