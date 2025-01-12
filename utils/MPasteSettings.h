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
    static const QString STAR_CATEGORY_NAME;

    const QString &getSaveDir() const;

    int getMaxSize() const;

    QNetworkProxy::ProxyType getProxyType() const;

    const QString &getProxyHost() const;

    int getPort() const;

    bool isAutoPaste() const;

    void setAutoPaste(bool autoPaste);

    const QString &getShortcutStr() const;

    void setShortcutStr(const QString &shortcutStr);

    bool isTerminalTitle(const QString &title);

    int getCurrFocusWinId() const;

    void setCurrFocusWinId(int currFocusWinId);

private:
    MPasteSettings();
    void loadSettings();
    void saveSettings();

    QStringList terminalNames;

    QString saveDir;
    int maxSize;

    QNetworkProxy::ProxyType proxyType;
    QString proxyHost;
    int port;

    bool autoPaste;
    QString shortcutStr;

    int currFocusWinId;

    static MPasteSettings *inst;
};


#endif //MPASTE_MPASTESETTINGS_H
