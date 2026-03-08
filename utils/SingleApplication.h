// input: 依赖 Qt 平台抽象、系统 API 与调用方声明。
// output: 对外提供 SingleApplication 的工具接口。
// pos: utils 层中的 SingleApplication 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
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