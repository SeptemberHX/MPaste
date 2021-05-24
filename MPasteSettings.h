//
// Created by ragdoll on 2021/5/24.
//

#ifndef MPASTE_MPASTESETTINGS_H
#define MPASTE_MPASTESETTINGS_H

#include <QString>

class MPasteSettings {

public:
    static MPasteSettings* getInst();

    const QString &getSaveDir() const;

    int getMaxSize() const;

private:
    MPasteSettings();

    QString saveDir;
    int maxSize;

    static MPasteSettings *inst;
};


#endif //MPASTE_MPASTESETTINGS_H
