// input: Depends on LocalSaver.h, Qt serialization primitives, and clipboard item metadata/MIME payloads.
// output: Implements current `.mpaste` v6 persistence with thumbnail + dedup, alias/pin metadata, and lazy MIME load.
// pos: Data-layer persistence implementation responsible for durable item storage and current-format reload.
// update: If I change, update this header block and my folder README.md.
//
// Created by ragdoll on 2021/5/24.
//

#include "LocalSaver.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QScreen>

namespace {
constexpr quint32 kLocalSaverVersionV4 = 4;
const QString kLocalSaverMagicV4 = QStringLiteral("MPASTE_CLIP_V4");
constexpr quint32 kLocalSaverFlagsV4 = 0;
constexpr quint32 kLocalSaverVersionV5 = 5;
const QString kLocalSaverMagicV5 = QStringLiteral("MPASTE_CLIP_V5");
constexpr quint32 kLocalSaverFlagsV5 = 0;
constexpr quint32 kLocalSaverVersionV6 = 6;
const QString kLocalSaverMagicV6 = QStringLiteral("MPASTE_CLIP_V6");
constexpr quint32 kLocalSaverFlagsV6 = 0;

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
                                 const QString &alias,
                                 bool pinned,
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
    item.setAlias(alias);
    item.setPinned(pinned);
    return item;
}

bool readCurrentFormatPrefix(QDataStream &in, quint32 &flags, quint32 &version) {
    QString magic;
    in >> magic >> version >> flags;
    return in.status() == QDataStream::Ok
        && ((magic == kLocalSaverMagicV6 && version == kLocalSaverVersionV6)
            || (magic == kLocalSaverMagicV5 && version == kLocalSaverVersionV5)
            || (magic == kLocalSaverMagicV4 && version == kLocalSaverVersionV4));
}

bool isCurrentFormatDevice(QIODevice *device) {
    if (!device || !device->isOpen()) {
        return false;
    }

    const qint64 originalPos = device->pos();
    QDataStream in(device);
    QString magic;
    quint32 version = 0;
    in >> magic >> version;
    const bool isCurrent = in.status() == QDataStream::Ok
        && ((magic == kLocalSaverMagicV6 && version == kLocalSaverVersionV6)
            || (magic == kLocalSaverMagicV5 && version == kLocalSaverVersionV5)
            || (magic == kLocalSaverMagicV4 && version == kLocalSaverVersionV4));
    device->seek(originalPos);
    return isCurrent;
}

qreal maxThumbnailDevicePixelRatio() {
    qreal maxDpr = 1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen) {
            maxDpr = qMax(maxDpr, screen->devicePixelRatio());
        }
    }
    return maxDpr;
}

QPixmap buildCardThumbnailPixmap(const QPixmap &fullImage) {
    if (fullImage.isNull()) {
        return QPixmap();
    }

    constexpr int cardW = 275;
    constexpr int cardH = 218;
    const qreal thumbnailDpr = maxThumbnailDevicePixelRatio();
    const QSize pixelTargetSize = QSize(cardW, cardH) * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return fullImage;
    }

    QPixmap scaled = fullImage.scaled(pixelTargetSize,
        Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return QPixmap();
    }

    const int x = qMax(0, (scaled.width() - pixelTargetSize.width()) / 2);
    const int y = qMax(0, (scaled.height() - pixelTargetSize.height()) / 2);
    QPixmap thumbnail = scaled.copy(x, y,
                                    qMin(scaled.width(), pixelTargetSize.width()),
                                    qMin(scaled.height(), pixelTargetSize.height()));
    thumbnail.setDevicePixelRatio(thumbnailDpr);
    return thumbnail;
}

bool shouldSkipImageSizeProbeFormat(const QString &format) {
    const QString lower = format.toLower();
    return lower.startsWith(QStringLiteral("text/"))
        || lower.contains(QStringLiteral("html"))
        || lower.contains(QStringLiteral("plain"))
        || lower.contains(QStringLiteral("xml"))
        || lower.contains(QStringLiteral("json"))
        || lower.contains(QStringLiteral("url"))
        || lower.contains(QStringLiteral("uri"))
        || lower.contains(QStringLiteral("descriptor"))
        || lower.contains(QStringLiteral("gvml"))
        || lower.contains(QStringLiteral("rtf"))
        || lower.contains(QStringLiteral("rich text"));
}

QSize probeImageSizeFromBytes(const QByteArray &data) {
    if (data.isEmpty()) {
        return {};
    }

    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    return reader.size();
}

QSize probeImageSizeFromMimeData(const QMimeData *mimeData) {
    if (!mimeData) {
        return {};
    }

    const QStringList formats = mimeData->formats();
    for (const QString &format : formats) {
        if (format.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))) {
            const QSize size = probeImageSizeFromBytes(mimeData->data(format));
            if (size.isValid()) {
                return size;
            }
        }
    }

    static const QStringList commonFormats = {
        QStringLiteral("image/png"),
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/gif"),
        QStringLiteral("image/bmp"),
        QStringLiteral("application/x-qt-image")
    };

    for (const QString &format : commonFormats) {
        if (mimeData->hasFormat(format)) {
            const QSize size = probeImageSizeFromBytes(mimeData->data(format));
            if (size.isValid()) {
                return size;
            }
        }
    }

    for (const QString &format : formats) {
        if (shouldSkipImageSizeProbeFormat(format)) {
            continue;
        }

        const QSize size = probeImageSizeFromBytes(mimeData->data(format));
        if (size.isValid()) {
            return size;
        }
    }

    const QVariant imageData = mimeData->imageData();
    if (imageData.canConvert<QPixmap>()) {
        const QPixmap pixmap = qvariant_cast<QPixmap>(imageData);
        if (!pixmap.isNull()) {
            return pixmap.size();
        }
    }
    if (imageData.canConvert<QImage>()) {
        const QImage image = qvariant_cast<QImage>(imageData);
        if (!image.isNull()) {
            return image.size();
        }
    }

    return {};
}

QSize probeImageSizeFromMimeSection(const QString &filePath, quint64 offset) {
    if (filePath.isEmpty()) {
        return {};
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    if (!file.seek(static_cast<qint64>(offset))) {
        file.close();
        return {};
    }

    QDataStream in(&file);
    quint32 formatCount = 0;
    in >> formatCount;
    if (in.status() != QDataStream::Ok) {
        file.close();
        return {};
    }

    QList<QByteArray> uniqueBlobs;
    for (quint32 i = 0; i < formatCount; ++i) {
        QString format;
        quint32 dataIndex = 0;
        QByteArray data;
        in >> format >> dataIndex >> data;
        if (in.status() != QDataStream::Ok) {
            file.close();
            return {};
        }

        QByteArray payload = data;
        if (!data.isEmpty()) {
            while (uniqueBlobs.size() <= static_cast<int>(dataIndex)) {
                uniqueBlobs.append(QByteArray());
            }
            uniqueBlobs[dataIndex] = data;
        } else if (static_cast<int>(dataIndex) < uniqueBlobs.size()) {
            payload = uniqueBlobs[dataIndex];
        }

        if (shouldSkipImageSizeProbeFormat(format)) {
            continue;
        }

        const QSize size = probeImageSizeFromBytes(payload);
        if (size.isValid()) {
            file.close();
            return size;
        }
    }

    file.close();
    return {};
}

bool formatMayContainSearchText(const QString &format) {
    const QString lower = format.toLower();
    return lower.startsWith(QStringLiteral("text/"))
        || lower.contains(QStringLiteral("html"))
        || lower.contains(QStringLiteral("plain"))
        || lower.contains(QStringLiteral("xml"))
        || lower.contains(QStringLiteral("json"))
        || lower.contains(QStringLiteral("url"))
        || lower.contains(QStringLiteral("uri"))
        || lower.contains(QStringLiteral("descriptor"))
        || lower.contains(QStringLiteral("ksdocclipboard"))
        || lower.contains(QStringLiteral("rich text"));
}

bool payloadContainsKeyword(const QByteArray &data, const QString &lowerKeyword) {
    if (data.isEmpty() || lowerKeyword.isEmpty()) {
        return false;
    }

    const QByteArray keywordUtf8 = lowerKeyword.toUtf8();
    if (!keywordUtf8.isEmpty() && data.toLower().contains(keywordUtf8)) {
        return true;
    }

    const QString utf8Text = QString::fromUtf8(data);
    if (!utf8Text.isEmpty() && utf8Text.toLower().contains(lowerKeyword)) {
        return true;
    }

    if (data.size() % 2 == 0) {
        const QString utf16Text = QString::fromUtf16(
            reinterpret_cast<const char16_t *>(data.constData()), data.size() / 2);
        if (!utf16Text.isEmpty() && utf16Text.toLower().contains(lowerKeyword)) {
            return true;
        }
    }

    return false;
}

bool skipBytes(QIODevice *device, qint64 bytes) {
    if (!device || bytes <= 0) {
        return true;
    }

    if (!device->isSequential()) {
        return device->seek(device->pos() + bytes);
    }

    constexpr qint64 kChunkSize = 64 * 1024;
    QByteArray buffer;
    buffer.resize(static_cast<int>(kChunkSize));
    qint64 remaining = bytes;
    while (remaining > 0) {
        const qint64 toRead = qMin(remaining, kChunkSize);
        const qint64 read = device->read(buffer.data(), toRead);
        if (read <= 0) {
            return false;
        }
        remaining -= read;
    }
    return true;
}

bool readByteArrayWithLimit(QDataStream &in, quint32 maxBytes, QByteArray *out) {
    quint32 size = 0;
    in >> size;
    if (in.status() != QDataStream::Ok) {
        return false;
    }

    QIODevice *device = in.device();
    if (!device) {
        in.setStatus(QDataStream::ReadCorruptData);
        return false;
    }

    if (size == 0) {
        if (out) {
            out->clear();
        }
        return true;
    }

    if (!out || maxBytes == 0) {
        if (!skipBytes(device, size)) {
            in.setStatus(QDataStream::ReadCorruptData);
            return false;
        }
        return true;
    }

    if (maxBytes > 0 && size > maxBytes) {
        if (out) {
            out->clear();
        }
        if (!skipBytes(device, size)) {
            in.setStatus(QDataStream::ReadCorruptData);
            return false;
        }
        return true;
    }

    out->resize(static_cast<int>(size));
    if (in.readRawData(out->data(), static_cast<int>(size)) != static_cast<int>(size)) {
        in.setStatus(QDataStream::ReadCorruptData);
        return false;
    }
    return true;
}

} // namespace

bool LocalSaver::mimeSectionContainsKeyword(const QString &filePath, quint64 offset, const QString &keyword) {
    if (filePath.isEmpty() || keyword.isEmpty()) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    if (!file.seek(static_cast<qint64>(offset))) {
        file.close();
        return false;
    }

    const QString lowerKeyword = keyword.toLower();
    QDataStream in(&file);
    quint32 formatCount = 0;
    in >> formatCount;
    if (in.status() != QDataStream::Ok) {
        file.close();
        return false;
    }

    constexpr quint32 kMaxSearchBytes = 2 * 1024 * 1024;
    QList<QByteArray> uniqueBlobs;
    for (quint32 i = 0; i < formatCount; ++i) {
        QString format;
        quint32 dataIndex = 0;
        in >> format >> dataIndex;
        if (in.status() != QDataStream::Ok) {
            file.close();
            return false;
        }

        while (uniqueBlobs.size() <= static_cast<int>(dataIndex)) {
            uniqueBlobs.append(QByteArray());
        }

        const bool shouldInspect = formatMayContainSearchText(format);
        QByteArray data;
        if (!readByteArrayWithLimit(in, shouldInspect ? kMaxSearchBytes : 0, &data)) {
            file.close();
            return false;
        }

        if (!data.isEmpty()) {
            uniqueBlobs[dataIndex] = data;
        }

        QByteArray payload = data;
        if (payload.isEmpty() && static_cast<int>(dataIndex) < uniqueBlobs.size()) {
            payload = uniqueBlobs[dataIndex];
        }

        if (shouldInspect && payloadContainsKeyword(payload, lowerKeyword)) {
            file.close();
            return true;
        }
    }

    file.close();
    return false;
}

namespace {

ClipboardItem loadCurrentFormat(const QByteArray &rawData) {
    if (rawData.isEmpty()) {
        return ClipboardItem();
    }

    QBuffer buffer;
    buffer.setData(rawData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return ClipboardItem();
    }

    QDataStream in(&buffer);
    quint32 flags = 0;
    quint32 version = 0;
    QDateTime time;
    QString name;
    QPixmap icon;
    QPixmap favicon;
    QString title;
    QString url;
    QString alias;
    bool pinned = false;
    quint32 contentType = 0;
    QString normalizedText;
    quint32 normalizedUrlCount = 0;
    QByteArray fingerprint;
    QPixmap thumbnail;
    quint64 mimeDataOffset = 0;
    quint32 formatCount = 0;

    if (!readCurrentFormatPrefix(in, flags, version)) {
        return ClipboardItem();
    }
    Q_UNUSED(flags);

    in >> time >> name >> icon >> favicon >> title >> url;
    if (version >= kLocalSaverVersionV5) {
        in >> alias;
        if (version >= kLocalSaverVersionV6) {
            in >> pinned;
        }
    }
    in >> contentType >> normalizedText >> normalizedUrlCount;
    for (quint32 i = 0; i < normalizedUrlCount; ++i) {
        QString ignoredUrl;
        in >> ignoredUrl;
    }
    in >> fingerprint >> thumbnail >> mimeDataOffset >> formatCount;
    if (in.status() != QDataStream::Ok) {
        return ClipboardItem();
    }
    Q_UNUSED(contentType);
    Q_UNUSED(normalizedText);
    Q_UNUSED(fingerprint);
    Q_UNUSED(mimeDataOffset);

    QList<QByteArray> uniqueBlobs;
    auto *mimeData = new QMimeData;
    for (quint32 i = 0; i < formatCount; ++i) {
        QString format;
        quint32 dataIndex = 0;
        QByteArray data;
        in >> format >> dataIndex >> data;
        if (in.status() != QDataStream::Ok) {
            delete mimeData;
            return ClipboardItem();
        }

        if (!data.isEmpty()) {
            while (uniqueBlobs.size() <= static_cast<int>(dataIndex)) {
                uniqueBlobs.append(QByteArray());
            }
            uniqueBlobs[dataIndex] = data;
            mimeData->setData(format, data);
        } else if (static_cast<int>(dataIndex) < uniqueBlobs.size()) {
            mimeData->setData(format, uniqueBlobs[dataIndex]);
        }
    }

    ClipboardItem item = finalizeLoadedItem(icon, time, name, favicon, title, url, alias, pinned, mimeData);
    if (!item.getName().isEmpty()) {
        item.setThumbnail(thumbnail);
    }
    return item;
}
}

bool LocalSaver::saveToFile(const ClipboardItem &item, const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    QDataStream out(&file);
    out << kLocalSaverMagicV6 << kLocalSaverVersionV6 << kLocalSaverFlagsV6;
    out << item.getTime() << item.getName() << item.getIcon();
    out << item.getFavicon() << item.getTitle() << item.getUrl() << item.getAlias() << item.isPinned();
    out << quint32(item.getContentType()) << item.getNormalizedText();

    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();
    out << quint32(normalizedUrls.size());
    for (const QUrl &url : normalizedUrls) {
        out << url.toString(QUrl::FullyEncoded);
    }

    out << item.fingerprint();

    // Generate and write a HiDPI-aware card thumbnail so list loads can avoid full image decode.
    QPixmap thumbnail = item.hasThumbnail() ? item.thumbnail() : QPixmap();
    if (thumbnail.isNull()) {
        QPixmap fullImage = item.getImage();
        if (!fullImage.isNull()) {
            thumbnail = buildCardThumbnailPixmap(fullImage);
        }
    }
    out << thumbnail;

    // Reserve space for mimeDataOffset, write placeholder, then come back
    const qint64 mimeDataOffsetPos = file.pos();
    quint64 mimeDataOffset = 0;
    out << mimeDataOffset;

    // Record actual MIME data start
    mimeDataOffset = static_cast<quint64>(file.pos());

    // Write MIME formats with dedup
    const QMimeData* mimeData = item.getMimeData();
    if (mimeData) {
        const QStringList formats = mimeData->formats();
        out << quint32(formats.size());

        // Track unique data blobs by SHA1 hash.
        QMap<QByteArray, quint32> hashToIndex;
        quint32 nextUniqueIndex = 0;

        for (const QString &format : formats) {
            const QByteArray data = mimeData->data(format);
            const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha1);

            auto it = hashToIndex.constFind(hash);
            if (it != hashToIndex.constEnd()) {
                out << format << it.value() << QByteArray();
            } else {
                quint32 index = nextUniqueIndex++;
                hashToIndex.insert(hash, index);
                out << format << index << data;
            }
        }
    } else {
        out << quint32(0);
    }

    file.seek(mimeDataOffsetPos);
    out << mimeDataOffset;

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

    const ClipboardItem item = loadFromRawData(rawData);
    if (item.getName().isEmpty()) {
        qWarning() << "Failed to load clipboard history file:" << filePath;
        return ClipboardItem();
    }

    return item;
}

ClipboardItem LocalSaver::loadFromRawData(const QByteArray &rawData) {
    return loadCurrentFormat(rawData);
}

namespace {
ClipboardItem loadFromStreamLight(QDataStream &in, const QString &filePath) {
    quint32 flags = 0;
    quint32 version = 0;
    if (!readCurrentFormatPrefix(in, flags, version)) {
        return ClipboardItem();
    }
    Q_UNUSED(flags);
    QDateTime time;
    QString name;
    QPixmap icon;
    QPixmap favicon;
    QString title;
    QString url;
    QString alias;
    bool pinned = false;
    quint32 contentType = 0;
    QString normalizedText;
    quint32 normalizedUrlCount = 0;
    QByteArray fingerprint;
    QPixmap thumbnail;
    quint64 mimeDataOffset = 0;

    in >> time >> name >> icon >> favicon >> title >> url;
    if (version >= kLocalSaverVersionV5) {
        in >> alias;
        if (version >= kLocalSaverVersionV6) {
            in >> pinned;
        }
    }
    in >> contentType >> normalizedText >> normalizedUrlCount;

    QList<QUrl> normalizedUrls;
    for (quint32 i = 0; i < normalizedUrlCount; ++i) {
        QString urlStr;
        in >> urlStr;
        normalizedUrls << QUrl(urlStr);
    }
    in >> fingerprint >> thumbnail >> mimeDataOffset;

    if (in.status() != QDataStream::Ok || name.isEmpty()) {
        return ClipboardItem();
    }

    // Build a light-loaded ClipboardItem (no MIME data, no full image decode).
    ClipboardItem item;
    ClipboardItem::ContentType effectiveType = static_cast<ClipboardItem::ContentType>(contentType);
    if (!thumbnail.isNull()
        && effectiveType == ClipboardItem::Text
        && normalizedText.trimmed().isEmpty()
        && normalizedUrls.isEmpty()) {
        effectiveType = ClipboardItem::Image;
    }
    item.setIcon(icon);
    item.setName(name);
    item.setTime(time);
    item.setTitle(title);
    item.setUrl(url);
    item.setAlias(alias);
    item.setPinned(pinned);
    item.setFavicon(favicon);
    item.setThumbnail(thumbnail);
    item.setSourceFilePath(filePath);
    item.setMimeDataFileOffset(mimeDataOffset);
    item.setFingerprintCache(fingerprint);
    item.setLightLoaded(
        effectiveType,
        normalizedText,
        normalizedUrls);

    return item;
}
}

ClipboardItem LocalSaver::loadFromRawDataLight(const QByteArray &rawData, const QString &sourceFilePath) {
    if (rawData.isEmpty()) {
        return ClipboardItem();
    }

    QBuffer buffer;
    buffer.setData(rawData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return ClipboardItem();
    }

    QDataStream in(&buffer);
    return loadFromStreamLight(in, sourceFilePath);
}

ClipboardItem LocalSaver::loadFromFileLight(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return ClipboardItem();
    }

    QDataStream in(&file);
    ClipboardItem item = loadFromStreamLight(in, filePath);
    file.close();
    return item;
}

bool LocalSaver::loadMimeSection(const QString &filePath, quint64 offset, ClipboardItem &item) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    if (!file.seek(static_cast<qint64>(offset))) {
        file.close();
        return false;
    }

    QDataStream in(&file);
    quint32 formatCount = 0;
    in >> formatCount;
    if (in.status() != QDataStream::Ok) {
        file.close();
        return false;
    }

    // Read MIME formats with dedup reconstruction.
    QList<QByteArray> uniqueBlobs;
    auto *mimeData = new QMimeData;
    for (quint32 i = 0; i < formatCount; ++i) {
        QString format;
        quint32 dataIndex = 0;
        QByteArray data;
        in >> format >> dataIndex >> data;
        if (in.status() != QDataStream::Ok) {
            delete mimeData;
            file.close();
            return false;
        }

        if (!data.isEmpty()) {
            while (uniqueBlobs.size() <= static_cast<int>(dataIndex)) {
                uniqueBlobs.append(QByteArray());
            }
            uniqueBlobs[dataIndex] = data;
            mimeData->setData(format, data);
        } else if (static_cast<int>(dataIndex) < uniqueBlobs.size()) {
            mimeData->setData(format, uniqueBlobs[dataIndex]);
        }
    }
    file.close();

    // Save metadata that would be overwritten by operator=.
    const QString savedName = item.getName();
    const QDateTime savedTime = item.getTime();
    const QString savedTitle = item.getTitle();
    const QString savedUrl = item.getUrl();
    const QPixmap savedFavicon = item.getFavicon();
    const QPixmap savedThumbnail = item.thumbnail();
    const QByteArray savedFingerprint = item.fingerprint();

    // Materialize canonical image formats through ClipboardItem(icon, mimeData)
    // and then restore the metadata that only exists on light-loaded items.
    ClipboardItem fullItem(item.getIcon(), mimeData);
    delete mimeData;

    item = fullItem;

    // Restore all preserved metadata.
    item.setName(savedName);
    item.setTime(savedTime);
    item.setTitle(savedTitle);
    item.setUrl(savedUrl);
    item.setFavicon(savedFavicon);
    item.setThumbnail(savedThumbnail);
    item.setFingerprintCache(savedFingerprint);
    return true;
}

bool LocalSaver::loadMimePayloads(const QString &filePath,
                                  quint64 offset,
                                  QString *htmlOut,
                                  QByteArray *imageOut) {
    if (filePath.isEmpty() || (!htmlOut && !imageOut)) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    if (!file.seek(static_cast<qint64>(offset))) {
        file.close();
        return false;
    }

    QDataStream in(&file);
    quint32 formatCount = 0;
    in >> formatCount;
    if (in.status() != QDataStream::Ok) {
        file.close();
        return false;
    }

    constexpr quint32 kMaxPreviewHtmlBytes = 2 * 1024 * 1024;
    constexpr quint32 kMaxPreviewImageBytes = 12 * 1024 * 1024;
    static const QStringList preferredImageFormats = {
        QStringLiteral("application/x-qt-image"),
        QStringLiteral("image/png"),
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/jpg"),
        QStringLiteral("image/webp"),
        QStringLiteral("image/gif"),
        QStringLiteral("image/bmp")
    };

    QList<QByteArray> uniqueBlobs;
    QByteArray htmlBytes;
    QByteArray imageBytes;

    for (quint32 i = 0; i < formatCount; ++i) {
        QString format;
        quint32 dataIndex = 0;
        in >> format >> dataIndex;
        if (in.status() != QDataStream::Ok) {
            file.close();
            return false;
        }

        while (uniqueBlobs.size() <= static_cast<int>(dataIndex)) {
            uniqueBlobs.append(QByteArray());
        }

        const bool wantsHtml = htmlOut && htmlBytes.isEmpty() && format == QStringLiteral("text/html");
        const bool wantsImage = imageOut && imageBytes.isEmpty()
            && (preferredImageFormats.contains(format) || format.startsWith(QStringLiteral("image/")));
        const bool shouldRead = wantsHtml || wantsImage;

        QByteArray data;
        const quint32 maxBytes = wantsHtml ? kMaxPreviewHtmlBytes
            : (wantsImage ? kMaxPreviewImageBytes : 0);
        if (!readByteArrayWithLimit(in, shouldRead ? maxBytes : 0, &data)) {
            file.close();
            return false;
        }

        if (!data.isEmpty()) {
            uniqueBlobs[dataIndex] = data;
        }

        QByteArray payload = data;
        if (payload.isEmpty() && static_cast<int>(dataIndex) < uniqueBlobs.size()) {
            payload = uniqueBlobs[dataIndex];
        }

        if (wantsHtml && htmlBytes.isEmpty()) {
            htmlBytes = payload;
        }
        if (wantsImage && imageBytes.isEmpty()) {
            imageBytes = payload;
        }

        if ((!htmlOut || !htmlBytes.isEmpty()) && (!imageOut || !imageBytes.isEmpty())) {
            // Collected everything we need.
            break;
        }
    }
    file.close();

    if (htmlOut) {
        *htmlOut = htmlBytes.isEmpty() ? QString() : QString::fromUtf8(htmlBytes);
    }
    if (imageOut) {
        *imageOut = imageBytes;
    }
    return true;
}

bool LocalSaver::isCurrentFormatFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const bool isCurrent = isCurrentFormatDevice(&file);
    file.close();
    return isCurrent;
}

bool LocalSaver::removeItem(const QString &filePath) {
    QFile file(filePath);
    return file.remove();
}

QSize ClipboardItem::getImagePixelSize() const {
    if (imageSizeCacheInitialized_) {
        return imageSizeCache_;
    }

    if (imageCacheInitialized_ && !imageCache_.isNull()) {
        imageSizeCache_ = imageCache_.size();
        imageSizeCacheInitialized_ = true;
        return imageSizeCache_;
    }

    if (!mimeDataLoaded_) {
        imageSizeCache_ = probeImageSizeFromMimeSection(sourceFilePath_, mimeDataFileOffset_);
        imageSizeCacheInitialized_ = true;
        return imageSizeCache_;
    }

    imageSizeCache_ = probeImageSizeFromMimeData(mimeData_.data());
    if (!imageSizeCache_.isValid()) {
        const QPixmap pixmap = getImage();
        if (!pixmap.isNull()) {
            imageSizeCache_ = pixmap.size();
        }
    }
    imageSizeCacheInitialized_ = true;
    return imageSizeCache_;
}

// Implementation of ClipboardItem::ensureMimeDataLoaded - placed here to avoid
// circular dependency (ClipboardItem.h cannot include LocalSaver.h).
void ClipboardItem::ensureMimeDataLoaded() {
    if (mimeDataLoaded_) {
        return;
    }

    mimeDataLoaded_ = true;

    if (sourceFilePath_.isEmpty()) {
        return;
    }

    LocalSaver::loadMimeSection(sourceFilePath_, mimeDataFileOffset_, *this);

    // Invalidate caches that were built from light-loaded metadata.
    imageCacheInitialized_ = false;
    imageSizeCacheInitialized_ = false;
    searchableTextCacheInitialized_ = false;
    normalizedUrlsCacheInitialized_ = false;
}

bool ClipboardItem::containsDeep(const QString &keyword) const {
    if (keyword.isEmpty()) {
        return false;
    }

    if (contains(keyword)) {
        return true;
    }

    if (mimeDataLoaded_ || sourceFilePath_.isEmpty()) {
        return false;
    }

    return LocalSaver::mimeSectionContainsKeyword(sourceFilePath_, mimeDataFileOffset_, keyword);
}
