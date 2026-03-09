// input: Depends on LocalSaver.h, Qt serialization primitives, and clipboard item metadata/MIME payloads.
// output: Implements `.mpaste` v3 persistence, backward-compatible loading, and on-disk migration helpers.
// pos: Data-layer persistence implementation responsible for durable item storage and format evolution.
// update: If I change, update this header block and my folder README.md.
//
// Created by ragdoll on 2021/5/24.
//

#include "LocalSaver.h"

#include <QBuffer>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

namespace {
constexpr quint32 kLocalSaverVersionV2 = 2;
constexpr quint32 kLocalSaverVersionV3 = 3;
const QString kLocalSaverMagicV2 = QStringLiteral("MPASTE_CLIP_V2");
const QString kLocalSaverMagicV3 = QStringLiteral("MPASTE_CLIP_V3");
constexpr quint32 kLocalSaverFlagsV3 = 0;

struct LoadResult {
    ClipboardItem item;
    bool recognized = false;
    bool needsMigration = false;
};

enum class SavedFormatVersion {
    Unknown,
    LegacyOrUnreadable,
    V2,
    V3,
};

bool hasMeaningfulMimeData(const QMimeData *mimeData) {
    if (!mimeData) {
        return false;
    }

    return mimeData->hasText()
        || mimeData->hasHtml()
        || mimeData->hasImage()
        || mimeData->hasUrls()
        || mimeData->hasColor()
        || !mimeData->formats().isEmpty();
}

ClipboardItem finalizeLoadedItem(const QPixmap &icon,
                                 const QDateTime &time,
                                 const QString &name,
                                 const QPixmap &favicon,
                                 const QString &title,
                                 const QString &url,
                                 QMimeData *mimeData) {
    if (!hasMeaningfulMimeData(mimeData) || name.isEmpty()) {
        delete mimeData;
        return ClipboardItem();
    }

    ClipboardItem item(icon, mimeData);
    delete mimeData;
    item.setName(name);
    item.setFavicon(favicon);
    item.setTime(time);
    item.setTitle(title);
    item.setUrl(url);
    return item;
}

LoadResult loadVersion3(const QByteArray &rawData) {
    QBuffer buffer;
    buffer.setData(rawData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QDataStream in(&buffer);
    QString magic;
    quint32 version = 0;
    quint32 flags = 0;
    QDateTime time;
    QString name;
    QPixmap icon;
    QPixmap favicon;
    QString title;
    QString url;
    quint32 contentType = 0;
    QString normalizedText;
    quint32 normalizedUrlCount = 0;
    QByteArray fingerprint;
    quint32 formatCount = 0;

    in >> magic >> version >> flags;
    if (in.status() != QDataStream::Ok || magic != kLocalSaverMagicV3 || version != kLocalSaverVersionV3) {
        return {};
    }

    in >> time >> name >> icon >> favicon >> title >> url;
    in >> contentType >> normalizedText >> normalizedUrlCount;
    for (quint32 i = 0; i < normalizedUrlCount; ++i) {
        QString ignoredUrl;
        in >> ignoredUrl;
    }
    in >> fingerprint >> formatCount;
    if (in.status() != QDataStream::Ok) {
        return {};
    }

    auto *mimeData = new QMimeData;
    for (quint32 i = 0; i < formatCount; ++i) {
        QString format;
        QByteArray data;
        in >> format >> data;
        if (in.status() != QDataStream::Ok) {
            delete mimeData;
            return {};
        }
        mimeData->setData(format, data);
    }

    LoadResult result;
    result.item = finalizeLoadedItem(icon, time, name, favicon, title, url, mimeData);
    result.recognized = !result.item.getName().isEmpty();
    return result;
}

LoadResult loadVersion2(const QByteArray &rawData) {
    QBuffer buffer;
    buffer.setData(rawData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QDataStream in(&buffer);
    QString magic;
    quint32 version = 0;
    QDateTime time;
    QString name;
    QPixmap icon;
    QPixmap favicon;
    QString title;
    QString url;
    quint32 formatCount = 0;

    in >> magic >> version;
    if (in.status() != QDataStream::Ok || magic != kLocalSaverMagicV2 || version != kLocalSaverVersionV2) {
        return {};
    }

    in >> time >> name >> icon >> favicon >> title >> url >> formatCount;
    if (in.status() != QDataStream::Ok) {
        return {};
    }

    auto *mimeData = new QMimeData;
    for (quint32 i = 0; i < formatCount; ++i) {
        QString format;
        QByteArray data;
        in >> format >> data;
        if (in.status() != QDataStream::Ok) {
            delete mimeData;
            return {};
        }
        mimeData->setData(format, data);
    }

    LoadResult result;
    result.item = finalizeLoadedItem(icon, time, name, favicon, title, url, mimeData);
    result.recognized = !result.item.getName().isEmpty();
    result.needsMigration = result.recognized;
    return result;
}

LoadResult loadLegacy(const QByteArray &rawData) {
    QBuffer buffer;
    buffer.setData(rawData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QDataStream in(&buffer);
    QDateTime time;
    QString name;
    QPixmap icon;
    QPixmap favicon;
    QString title;
    QString url;
    in >> time >> name >> icon >> favicon >> title >> url;
    if (in.status() != QDataStream::Ok) {
        return {};
    }

    auto *mimeData = new QMimeData;
    while (!in.atEnd()) {
        QString format;
        QByteArray data;
        in >> format >> data;
        if (in.status() != QDataStream::Ok) {
            delete mimeData;
            return {};
        }
        mimeData->setData(format, data);
    }

    LoadResult result;
    result.item = finalizeLoadedItem(icon, time, name, favicon, title, url, mimeData);
    result.recognized = !result.item.getName().isEmpty();
    result.needsMigration = result.recognized;
    return result;
}

LoadResult loadAnyVersion(const QByteArray &rawData) {
    if (rawData.isEmpty()) {
        return {};
    }

    LoadResult result = loadVersion3(rawData);
    if (result.recognized) {
        return result;
    }

    result = loadVersion2(rawData);
    if (result.recognized) {
        return result;
    }

    return loadLegacy(rawData);
}

SavedFormatVersion detectSavedFormatVersion(QIODevice *device) {
    if (!device || !device->isOpen()) {
        return SavedFormatVersion::Unknown;
    }

    const qint64 originalPos = device->pos();
    QDataStream in(device);
    QString magic;
    quint32 version = 0;
    in >> magic >> version;
    device->seek(originalPos);

    if (in.status() != QDataStream::Ok) {
        return SavedFormatVersion::LegacyOrUnreadable;
    }

    if (magic == kLocalSaverMagicV3 && version == kLocalSaverVersionV3) {
        return SavedFormatVersion::V3;
    }

    if (magic == kLocalSaverMagicV2 && version == kLocalSaverVersionV2) {
        return SavedFormatVersion::V2;
    }

    return SavedFormatVersion::LegacyOrUnreadable;
}
}

bool LocalSaver::saveToFile(const ClipboardItem &item, const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    QDataStream out(&file);
    out << kLocalSaverMagicV3 << kLocalSaverVersionV3 << kLocalSaverFlagsV3;
    out << item.getTime() << item.getName() << item.getIcon();
    out << item.getFavicon() << item.getTitle() << item.getUrl();
    out << quint32(item.getContentType()) << item.getNormalizedText();

    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();
    out << quint32(normalizedUrls.size());
    for (const QUrl &url : normalizedUrls) {
        out << url.toString(QUrl::FullyEncoded);
    }

    out << item.fingerprint();

    const QMimeData* mimeData = item.getMimeData();
    if (mimeData) {
        const QStringList formats = mimeData->formats();
        out << quint32(formats.size());
        for (const QString &format : formats) {
            out << format << mimeData->data(format);
        }
    } else {
        out << quint32(0);
    }

    file.close();
    return out.status() == QDataStream::Ok;
}

ClipboardItem LocalSaver::loadFromFile(const QString &filePath) {
    QElapsedTimer timer;
    timer.start();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return ClipboardItem();
    }

    const QByteArray rawData = file.readAll();
    file.close();

    const ClipboardItem item = loadFromRawData(rawData);
    if (item.getName().isEmpty()) {
        qWarning() << "Failed to load clipboard history file:" << filePath;
        return ClipboardItem();
    }

    const qint64 elapsedMs = timer.elapsed();
    if (elapsedMs >= 20 || rawData.size() >= 256 * 1024) {
        qInfo().noquote() << QStringLiteral("[storage] loadFromFile %1 size=%2 KB took %3 ms")
                                 .arg(QFileInfo(filePath).fileName())
                                 .arg(QString::number(rawData.size() / 1024.0, 'f', 1))
                                 .arg(elapsedMs);
    }

    return item;
}

ClipboardItem LocalSaver::loadFromRawData(const QByteArray &rawData) {
    const LoadResult result = loadAnyVersion(rawData);
    return result.recognized ? result.item : ClipboardItem();
}

bool LocalSaver::migrateFileToCurrentVersion(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const SavedFormatVersion detectedVersion = detectSavedFormatVersion(&file);
    if (detectedVersion == SavedFormatVersion::V3) {
        file.close();
        return true;
    }

    const QByteArray rawData = file.readAll();
    file.close();

    const LoadResult result = loadAnyVersion(rawData);
    if (!result.recognized) {
        return false;
    }

    if (!result.needsMigration) {
        return true;
    }

    return saveToFile(result.item, filePath);
}

void LocalSaver::migrateDirectory(const QString &dirPath) {
    QElapsedTimer timer;
    timer.start();
    QDir dir(dirPath);
    const QFileInfoList files = dir.entryInfoList(QStringList() << "*.mpaste", QDir::Files);
    int failedCount = 0;
    for (const QFileInfo &info : files) {
        if (!migrateFileToCurrentVersion(info.filePath())) {
            ++failedCount;
            qWarning() << "Failed to migrate clipboard history file to v3:" << info.filePath();
        }
    }
    qInfo().noquote() << QStringLiteral("[storage] migrateDirectory %1 files=%2 failed=%3 took %4 ms")
                             .arg(dirPath)
                             .arg(files.size())
                             .arg(failedCount)
                             .arg(timer.elapsed());
}

bool LocalSaver::removeItem(const QString &filePath) {
    QFile file(filePath);
    return file.remove();
}
