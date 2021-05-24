//
// Created by ragdoll on 2021/5/24.
//

#ifndef MPASTE_LOCALSAVER_H
#define MPASTE_LOCALSAVER_H

#include <QString>
#include "ClipboardItem.h"

class LocalSaver {

public:
    bool saveToFile(const ClipboardItem &item, const QString &filePath);
    ClipboardItem loadFromFile(const QString &filePath);
};


#endif //MPASTE_LOCALSAVER_H
