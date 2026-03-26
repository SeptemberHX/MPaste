// input: Depends on ClipboardItem data, file-system paths, and LocalSaver persistence rules.
// output: Exposes current `.mpaste` save/load entry points and lazy MIME reload hooks.
// pos: Data-layer persistence interface for clipboard item files.
// update: If I change, update this header block and my folder README.md.
//
// Created by ragdoll on 2021/5/24.
//

#ifndef MPASTE_LOCALSAVER_H
#define MPASTE_LOCALSAVER_H

#include <QByteArray>
#include <QString>
#include "ClipboardItem.h"

class LocalSaver {

public:
    bool saveToFile(const ClipboardItem &item, const QString &filePath);
    bool removeItem(const QString &filePath);
    ClipboardItem loadFromFile(const QString &filePath);
    ClipboardItem loadFromRawData(const QByteArray &rawData);
    ClipboardItem loadFromRawDataLight(const QByteArray &rawData,
                                       const QString &sourceFilePath,
                                       bool includeThumbnail = false);
    ClipboardItem loadFromFileLight(const QString &filePath, bool includeThumbnail = false);
    bool updateMetadata(const QString &filePath, const QString &alias, bool pinned);
    bool updateThumbnail(const QString &filePath, const QPixmap &thumbnail);
    static bool loadMimeSection(const QString &filePath, quint64 offset, ClipboardItem &item);
    static bool loadMimePayloads(const QString &filePath,
                                 quint64 offset,
                                 QString *htmlOut,
                                 QByteArray *imageOut);
    static bool mimeSectionContainsKeyword(const QString &filePath, quint64 offset, const QString &keyword);
    static bool isCurrentFormatFile(const QString &filePath);
};


#endif //MPASTE_LOCALSAVER_H
