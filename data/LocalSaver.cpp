// input: 依赖对应头文件、Qt 序列化/网络/图像能力。
// output: 对外提供 LocalSaver 的数据实现。
// pos: data 层中的 LocalSaver 实现文件。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
//
// Created by ragdoll on 2021/5/24.
//

#include "LocalSaver.h"

#include <QBuffer>
#include <QDataStream>
#include <QDebug>
#include <QFile>

namespace {
constexpr quint32 kLocalSaverVersion = 2;
const QString kLocalSaverMagic = QStringLiteral("MPASTE_CLIP_V2");

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

ClipboardItem loadVersion2(const QByteArray &rawData) {
    QBuffer buffer;
    buffer.setData(rawData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return ClipboardItem();
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
    if (in.status() != QDataStream::Ok || magic != kLocalSaverMagic || version != kLocalSaverVersion) {
        return ClipboardItem();
    }

    in >> time >> name >> icon >> favicon >> title >> url >> formatCount;
    if (in.status() != QDataStream::Ok) {
        return ClipboardItem();
    }

    auto *mimeData = new QMimeData;
    for (quint32 i = 0; i < formatCount; ++i) {
        QString format;
        QByteArray data;
        in >> format >> data;
        if (in.status() != QDataStream::Ok) {
            delete mimeData;
            return ClipboardItem();
        }
        mimeData->setData(format, data);
    }

    return finalizeLoadedItem(icon, time, name, favicon, title, url, mimeData);
}

ClipboardItem loadLegacy(const QByteArray &rawData) {
    QBuffer buffer;
    buffer.setData(rawData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return ClipboardItem();
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
        return ClipboardItem();
    }

    auto *mimeData = new QMimeData;
    while (!in.atEnd()) {
        QString format;
        QByteArray data;
        in >> format >> data;
        if (in.status() != QDataStream::Ok) {
            delete mimeData;
            return ClipboardItem();
        }
        mimeData->setData(format, data);
    }

    return finalizeLoadedItem(icon, time, name, favicon, title, url, mimeData);
}
}

bool LocalSaver::saveToFile(const ClipboardItem &item, const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QDataStream out(&file);
    out << kLocalSaverMagic << kLocalSaverVersion;
    out << item.getTime() << item.getName() << item.getIcon();
    out << item.getFavicon() << item.getTitle() << item.getUrl();

    const QMimeData* mimeData = item.getMimeData();
    if (mimeData) {
        QStringList formats = mimeData->formats();
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
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return ClipboardItem();
    }

    const QByteArray rawData = file.readAll();
    file.close();

    ClipboardItem item = loadVersion2(rawData);
    if (!item.getName().isEmpty()) {
        return item;
    }

    item = loadLegacy(rawData);
    if (item.getName().isEmpty()) {
        qWarning() << "Failed to load clipboard history file:" << filePath;
    }

    return item;
}

bool LocalSaver::removeItem(const QString &filePath) {
    QFile file(filePath);
    return file.remove();
}
