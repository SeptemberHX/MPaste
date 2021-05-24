//
// Created by ragdoll on 2021/5/24.
//

#include "MPasteSettings.h"
#include <QStandardPaths>
#include <QDir>
#include <iostream>

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
{

}

int MPasteSettings::getMaxSize() const {
    return maxSize;
}
