// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 SingleApplication 的声明接口。
// pos: utils 层中的 SingleApplication 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
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
