// input: 依赖 Qt 平台抽象、系统 API 与调用方声明。
// output: 对外提供 MPasteSettings 的工具接口。
// pos: utils 层中的 MPasteSettings 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
//
// Created by ragdoll on 2021/5/24.
//

#ifndef MPASTE_MPASTESETTINGS_H
#define MPASTE_MPASTESETTINGS_H

#include <QString>
#include <QNetworkProxy>

class MPasteSettings : public QObject {
    Q_OBJECT

public:
    static MPasteSettings* getInst();

    static const QString CLIPBOARD_CATEGORY_NAME;
    static const QString CLIPBOARD_CATEGORY_COLOR;
    static const QString STAR_CATEGORY_NAME;
    static const QString STAR_CATEGORY_COLOR;

    const QString &getSaveDir() const;

    int getMaxSize() const;

    QNetworkProxy::ProxyType getProxyType() const;

    const QString &getProxyHost() const;

    int getPort() const;

    bool isAutoPaste() const;

    void setAutoPaste(bool autoPaste);

    const QString &getShortcutStr() const;

    void setShortcutStr(const QString &shortcutStr);

    void setMaxSize(int maxSize);

    int getItemScale() const;
    void setItemScale(int itemScale);

    bool isPlaySound() const;
    void setPlaySound(bool playSound);

    bool isTerminalTitle(const QString &title);

    int getCurrFocusWinId() const;

    void setCurrFocusWinId(int currFocusWinId);

    void saveSettings();

private:
    MPasteSettings();
    void loadSettings();

    QStringList terminalNames;

    QString saveDir;
    int maxSize;

    QNetworkProxy::ProxyType proxyType;
    QString proxyHost;
    int port;

    bool autoPaste;
    QString shortcutStr;
    int itemScale;
    bool playSound;

    int currFocusWinId;

    static MPasteSettings *inst;
};


#endif //MPASTE_MPASTESETTINGS_H
