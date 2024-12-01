#ifndef MPASTE_SINGLEAPPLICATION_H
#define MPASTE_SINGLEAPPLICATION_H

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>

class SingleApplication : public QObject {
    Q_OBJECT

public:
    explicit SingleApplication(const QString &appId, QObject *parent = nullptr);
    ~SingleApplication();

    bool isPrimaryInstance();
    bool sendMessage(const QString &message);

    signals:
        void messageReceived(const QString &message);

    private slots:
        void handleNewConnection();
    void handleSocketError(QLocalSocket::LocalSocketError error);
    void readMessage();

private:
    bool startPrimaryInstance();
    void cleanup();

    QString serverName;
    QLocalServer *server;
    QLocalSocket *socket;
    bool isPrimary;
};

#endif //MPASTE_SINGLEAPPLICATION_H