//
// Created by ragdoll on 2021/5/24.
//

#ifndef MPASTE_MPASTESETTINGS_H
#define MPASTE_MPASTESETTINGS_H

#include <QString>
#include <QNetworkProxy>

class MPasteSettings {

public:
    static MPasteSettings* getInst();

    const QString &getSaveDir() const;

    int getMaxSize() const;

    QNetworkProxy::ProxyType getProxyType() const;

    const QString &getProxyHost() const;

    int getPort() const;

private:
    MPasteSettings();
    void loadSettings();
    void saveSettings();

    QString saveDir;
    int maxSize;

    QNetworkProxy::ProxyType proxyType;
    QString proxyHost;
    int port;

    static MPasteSettings *inst;
};


#endif //MPASTE_MPASTESETTINGS_H
