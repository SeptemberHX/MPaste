#include "SingleApplication.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QThread>

SingleApplication::SingleApplication(const QString &appId, QObject *parent)
    : QObject(parent)
    , server(nullptr)
    , socket(nullptr)
    , isPrimary(false)
{
    // Create a unique server name based on the user name and app ID
    QString userName = qgetenv("USER");
    if (userName.isEmpty())
        userName = qgetenv("USERNAME");
    
    // Create a hash of the username and app ID to ensure unique server name
    QByteArray hash = QCryptographicHash::hash(
        (userName + appId).toUtf8(),
        QCryptographicHash::Sha256
    );
    serverName = "MPaste-" + hash.toHex().left(8);

    // Try to start as primary instance
    isPrimary = startPrimaryInstance();
}

SingleApplication::~SingleApplication()
{
    cleanup();
}

bool SingleApplication::startPrimaryInstance()
{
    // Try to connect to existing server
    socket = new QLocalSocket(this);
    socket->connectToServer(serverName);
    
    if (socket->waitForConnected(500)) {
        // Another instance is already running
        return false;
    }

    // No existing server found, start new one
    delete socket;
    socket = nullptr;

    cleanup(); // Remove any existing server
    
    server = new QLocalServer(this);
    
    // Ensure the server name is unique
    if (!server->listen(serverName)) {
        if (server->serverError() == QAbstractSocket::AddressInUseError) {
            QLocalServer::removeServer(serverName);
            if (!server->listen(serverName)) {
                return false;
            }
        }
    }

    connect(server, &QLocalServer::newConnection,
            this, &SingleApplication::handleNewConnection);
    
    return true;
}

void SingleApplication::cleanup()
{
    if (socket) {
        socket->disconnectFromServer();
        delete socket;
        socket = nullptr;
    }

    if (server) {
        server->close();
        delete server;
        server = nullptr;
    }
}

bool SingleApplication::isPrimaryInstance()
{
    return isPrimary;
}

bool SingleApplication::sendMessage(const QString &message)
{
    if (isPrimary)
        return false;

    if (!socket)
        return false;

    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << message;

    socket->write(data);
    bool sent = socket->waitForBytesWritten(1000);
    
    // Wait a bit to ensure the primary instance processes the message
    QThread::msleep(100);
    
    return sent;
}

void SingleApplication::handleNewConnection()
{
    QLocalSocket *clientSocket = server->nextPendingConnection();
    connect(clientSocket, &QLocalSocket::readyRead, this, &SingleApplication::readMessage);
    connect(clientSocket, &QLocalSocket::disconnected,
            clientSocket, &QLocalSocket::deleteLater);
}

void SingleApplication::handleSocketError(QLocalSocket::LocalSocketError error)
{
    qWarning() << "Socket error:" << error;
}

void SingleApplication::readMessage()
{
    QLocalSocket *clientSocket = qobject_cast<QLocalSocket*>(sender());
    if (!clientSocket)
        return;

    QByteArray data = clientSocket->readAll();
    QDataStream stream(data);
    QString message;
    stream >> message;

    emit messageReceived(message);
}