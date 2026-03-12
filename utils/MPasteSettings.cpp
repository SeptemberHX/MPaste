// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 MPasteSettings 的实现逻辑。
// pos: utils 层中的 MPasteSettings 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
//
// Created by ragdoll on 2021/5/24.
//

#include "MPasteSettings.h"
#include <QStandardPaths>
#include <QDir>
#include <iostream>
#include <QSettings>

MPasteSettings *MPasteSettings::inst = nullptr;

const QString MPasteSettings::CLIPBOARD_CATEGORY_NAME = "Clipboard";
const QString MPasteSettings::CLIPBOARD_CATEGORY_COLOR = "#4A90E2";

const QString MPasteSettings::STAR_CATEGORY_NAME = "Stared";
const QString MPasteSettings::STAR_CATEGORY_COLOR = "#fc9867";

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
    , historyRetentionValue(30)
    , historyRetentionUnit(RetentionDays)
    , proxyType(QNetworkProxy::NoProxy)
{
    this->proxyType = QNetworkProxy::HttpProxy;
    this->proxyHost = "127.0.0.1";
    this->port = 7890;

    this->autoPaste = true;
    this->pasteShortcutMode = AutoPasteShortcut;
    this->shortcutStr = "Alt+Q";
    this->itemScale = 100;
    this->playSound = true;

    this->terminalNames << tr("Terminal");

    this->loadSettings();

    // this helps to create the configuration file
    this->saveSettings();
}

int MPasteSettings::getMaxSize() const {
    return maxSize;
}

int MPasteSettings::getHistoryRetentionValue() const {
    return historyRetentionValue;
}

MPasteSettings::HistoryRetentionUnit MPasteSettings::getHistoryRetentionUnit() const {
    return historyRetentionUnit;
}

QDateTime MPasteSettings::historyRetentionCutoff(const QDateTime &reference) const {
    const int value = qMax(1, historyRetentionValue);
    switch (historyRetentionUnit) {
        case RetentionWeeks:
            return reference.addDays(-(value * 7));
        case RetentionMonths:
            return reference.addMonths(-value);
        case RetentionDays:
        default:
            return reference.addDays(-value);
    }
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
    this->historyRetentionValue = settings.value("main/historyRetentionValue", this->historyRetentionValue).toInt();
    this->historyRetentionUnit = static_cast<HistoryRetentionUnit>(
        settings.value("main/historyRetentionUnit", static_cast<int>(this->historyRetentionUnit)).toInt());
    this->saveDir = settings.value("main/saveDir", this->saveDir).toString();
    this->autoPaste = settings.value("main/autoPaste", this->autoPaste).toBool();
    this->pasteShortcutMode = static_cast<PasteShortcutMode>(settings.value("main/pasteShortcutMode", static_cast<int>(this->pasteShortcutMode)).toInt());
    this->shortcutStr = settings.value("main/shortcut", this->shortcutStr).toString();
    this->itemScale = settings.value("main/itemScale", this->itemScale).toInt();
    this->playSound = settings.value("main/playSound", this->playSound).toBool();
}

void MPasteSettings::saveSettings() {
    QSettings settings("MPaste", "MPaste");

    settings.setValue("main/historySize", this->maxSize);
    settings.setValue("main/historyRetentionValue", this->historyRetentionValue);
    settings.setValue("main/historyRetentionUnit", static_cast<int>(this->historyRetentionUnit));
    settings.setValue("main/saveDir", this->saveDir);
    settings.setValue("main/autoPaste", this->autoPaste);
    settings.setValue("main/pasteShortcutMode", static_cast<int>(this->pasteShortcutMode));
    settings.setValue("main/shortcut", this->shortcutStr);
    settings.setValue("main/itemScale", this->itemScale);
    settings.setValue("main/playSound", this->playSound);
}

bool MPasteSettings::isAutoPaste() const {
    return autoPaste;
}

void MPasteSettings::setAutoPaste(bool autoPaste) {
    MPasteSettings::autoPaste = autoPaste;
}

MPasteSettings::PasteShortcutMode MPasteSettings::getPasteShortcutMode() const {
    return pasteShortcutMode;
}

void MPasteSettings::setPasteShortcutMode(MPasteSettings::PasteShortcutMode mode) {
    pasteShortcutMode = mode;
}

QString MPasteSettings::pasteShortcutModeLabel(MPasteSettings::PasteShortcutMode mode) {
    switch (mode) {
        case CtrlVShortcut:
            return QStringLiteral("Ctrl+V");
        case ShiftInsertShortcut:
            return QStringLiteral("Shift+Insert");
        case CtrlShiftVShortcut:
            return QStringLiteral("Ctrl+Shift+V");
        case AltInsertShortcut:
            return QStringLiteral("Alt+Insert");
        case AutoPasteShortcut:
        default:
            return QStringLiteral("Auto");
    }
}

const QString &MPasteSettings::getShortcutStr() const {
    return shortcutStr;
}

void MPasteSettings::setShortcutStr(const QString &shortcutStr) {
    MPasteSettings::shortcutStr = shortcutStr;
}

void MPasteSettings::setMaxSize(int maxSize) {
    MPasteSettings::maxSize = maxSize;
}

void MPasteSettings::setHistoryRetentionValue(int value) {
    historyRetentionValue = value;
}

void MPasteSettings::setHistoryRetentionUnit(MPasteSettings::HistoryRetentionUnit unit) {
    historyRetentionUnit = unit;
}

int MPasteSettings::getItemScale() const {
    return itemScale;
}

void MPasteSettings::setItemScale(int itemScale) {
    MPasteSettings::itemScale = itemScale;
}

bool MPasteSettings::isPlaySound() const {
    return playSound;
}

void MPasteSettings::setPlaySound(bool playSound) {
    MPasteSettings::playSound = playSound;
}

bool MPasteSettings::isTerminalTitle(const QString &title) {
    return this->terminalNames.contains(title);
}

int MPasteSettings::getCurrFocusWinId() const {
    return currFocusWinId;
}

void MPasteSettings::setCurrFocusWinId(int currFocusWinId) {
    MPasteSettings::currFocusWinId = currFocusWinId;
}
