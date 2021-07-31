//
// Created by ragdoll on 2021/5/24.
//

#include "MPasteSettings.h"
#include <QStandardPaths>
#include <QDir>
#include <iostream>
#include <QSettings>

MPasteSettings *MPasteSettings::inst = nullptr;

MPasteSettings *MPasteSettings::getInst() {
    if (inst == nullptr) {
        MPasteSettings::inst = new MPasteSettings();
    }
    return inst;
}

const QString &MPasteSettings::getSaveDir() const {
    return saveDir;
}

MPasteSettings::MPasteSettings()
    : saveDir(QDir::homePath() + QDir::separator() +  ".MPaste")
    , maxSize(500)
    , proxyType(QNetworkProxy::NoProxy)
{
    this->proxyType = QNetworkProxy::HttpProxy;
    this->proxyHost = "127.0.0.1";
    this->port = 7890;

    this->loadSettings();

    // this helps to create the configuration file
    this->saveSettings();
}

int MPasteSettings::getMaxSize() const {
    return maxSize;
}

QNetworkProxy::ProxyType MPasteSettings::getProxyType() const {
    return proxyType;
}

const QString &MPasteSettings::getProxyHost() const {
    return proxyHost;
}

int MPasteSettings::getPort() const {
    return port;
}

void MPasteSettings::loadSettings() {
    QSettings settings("MPaste", "MPaste");

    this->maxSize = settings.value("main/historySize", this->maxSize).toInt();
    this->saveDir = settings.value("main/saveDir", this->saveDir).toString();
}

void MPasteSettings::saveSettings() {
    QSettings settings("MPaste", "MPaste");

    settings.setValue("main/historySize", this->maxSize);
    settings.setValue("main/saveDir", this->saveDir);
}
