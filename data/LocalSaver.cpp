// input: Depends on LocalSaver.h, Qt serialization primitives, and clipboard item metadata/MIME payloads.
// output: Implements current `.mpaste` v6 persistence with thumbnail + dedup, alias/pin metadata, and lazy MIME load.
// pos: Data-layer persistence implementation responsible for durable item storage and current-format reload.
// update: If I change, update this header block and my folder README.md.
// note: Preserve alias/pin metadata, avoid empty MIME payloads when rehydrating, and use shared card preview metrics.
//
// Created by ragdoll on 2021/5/24.
//

#include "LocalSaver.h"

#include "CardPreviewMetrics.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QScopedPointer>
#include <QScreen>
#include <QSaveFile>

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

    if (mimeData->hasText() && !mimeData->text().isEmpty()) {
        return true;
    }
    if (mimeData->hasHtml() && !mimeData->html().isEmpty()) {
        return true;
    }
    if (mimeData->hasUrls() && !mimeData->urls().isEmpty()) {
        return true;
    }
    if (mimeData->hasColor()) {
        return true;
    }
    if (mimeData->hasImage()) {
        const QVariant imageData = mimeData->imageData();
        if (imageData.isValid() && !imageData.isNull()) {
            return true;
        }
    }
    for (const QString &format : mimeData->formats()) {
        if (!mimeData->data(format).isEmpty()) {
            return true;
        }
    }
    return false;
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

struct CurrentFormatHeader {
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
    QList<QUrl> normalizedUrls;
    QByteArray fingerprint;
    QPixmap thumbnail;
    quint64 mimeDataOffset = 0;
};

bool readCurrentFormatHeader(QDataStream &in, quint32 version, CurrentFormatHeader &header) {
    in >> header.time >> header.name >> header.icon >> header.favicon >> header.title >> header.url;
    if (version >= kLocalSaverVersionV5) {
        in >> header.alias;
        if (version >= kLocalSaverVersionV6) {
            in >> header.pinned;
        }
    }
    quint32 normalizedUrlCount = 0;
    in >> header.contentType >> header.normalizedText >> normalizedUrlCount;
    header.normalizedUrls.clear();
    header.normalizedUrls.reserve(static_cast<int>(normalizedUrlCount));
    for (quint32 i = 0; i < normalizedUrlCount; ++i) {
        QString urlStr;
        in >> urlStr;
        header.normalizedUrls << QUrl(urlStr);
    }
    in >> header.fingerprint >> header.thumbnail >> header.mimeDataOffset;
    return in.status() == QDataStream::Ok && !header.name.isEmpty();
}

qint64 writeCurrentFormatHeader(QDataStream &out, const CurrentFormatHeader &header, quint64 mimeDataOffsetPlaceholder = 0) {
    out << kLocalSaverMagicV6 << kLocalSaverVersionV6 << kLocalSaverFlagsV6;
    out << header.time << header.name << header.icon << header.favicon << header.title << header.url;
    out << header.alias << header.pinned;
    out << header.contentType << header.normalizedText;
    out << quint32(header.normalizedUrls.size());
    for (const QUrl &url : header.normalizedUrls) {
        out << url.toString(QUrl::FullyEncoded);
    }
    out << header.fingerprint;
    out << header.thumbnail;
    const qint64 mimeOffsetPos = out.device() ? out.device()->pos() : -1;
    out << mimeDataOffsetPlaceholder;
    return mimeOffsetPos;
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

    constexpr int cardW = kCardPreviewWidth;
    constexpr int cardH = kCardPreviewHeight;
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

    for (const QString &format : ContentClassifier::commonImageFormats()) {
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

    // QDataStream encodes a null QByteArray as 0xFFFFFFFF and an empty
    // one as size 0.  Both mean "no data for this entry".
    if (size == 0 || size == 0xFFFFFFFF) {
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
    CurrentFormatHeader header;
    quint32 formatCount = 0;

    if (!readCurrentFormatPrefix(in, flags, version)) {
        return ClipboardItem();
    }
    Q_UNUSED(flags);

    if (!readCurrentFormatHeader(in, version, header)) {
        return ClipboardItem();
    }
    in >> formatCount;
    if (in.status() != QDataStream::Ok) {
        return ClipboardItem();
    }

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

    if (!hasMeaningfulMimeData(mimeData)) {
        if (!header.normalizedText.isEmpty()) {
            mimeData->setText(header.normalizedText);
        }
        if (!header.normalizedUrls.isEmpty()) {
            mimeData->setUrls(header.normalizedUrls);
        }
    }

    ClipboardItem item = finalizeLoadedItem(header.icon,
                                            header.time,
                                            header.name,
                                            header.favicon,
                                            header.title,
                                            header.url,
                                            header.alias,
                                            header.pinned,
                                            mimeData);
    if (!item.getName().isEmpty()) {
        item.setThumbnail(header.thumbnail);
    }
    return item;
}
}

bool LocalSaver::saveToFile(const ClipboardItem &item, const QString &filePath,
                           const QImage &thumbnailOverride) {
    // Resolve thumbnail: explicit override > item's thumbnail > generate
    // from image data > preserve from existing file on disk.
    QPixmap thumbnail;
    if (!thumbnailOverride.isNull()) {
        thumbnail = QPixmap::fromImage(thumbnailOverride);
        thumbnail.setDevicePixelRatio(thumbnailOverride.devicePixelRatio());
    } else if (item.hasThumbnail()) {
        thumbnail = item.thumbnail();
    } else {
        QPixmap fullImage = item.getImage();
        if (!fullImage.isNull()) {
            thumbnail = buildCardThumbnailPixmap(fullImage);
        }
    }
    // Last resort: read thumbnail from existing file before we overwrite it.
    if (thumbnail.isNull() && QFileInfo::exists(filePath)) {
        ClipboardItem existing = loadFromFileLight(filePath, true);
        if (existing.hasThumbnail()) {
            thumbnail = existing.thumbnail();
        }
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[LocalSaver] saveToFile failed: path=" << filePath
                    << "reason=cannot open for writing";
        return false;
    }

    CurrentFormatHeader header;
    header.time = item.getTime();
    header.name = item.getName();
    header.icon = item.getIcon();
    header.favicon = item.getFavicon();
    header.title = item.getTitle();
    header.url = item.getUrl();
    header.alias = item.getAlias();
    header.pinned = item.isPinned();
    header.contentType = quint32(item.getContentType());
    header.normalizedText = item.getNormalizedText();
    header.normalizedUrls = item.getNormalizedUrls();
    header.fingerprint = item.fingerprint();
    header.thumbnail = thumbnail;

    QDataStream out(&file);
    const qint64 mimeDataOffsetPos = writeCurrentFormatHeader(out, header);

    quint64 mimeDataOffset = static_cast<quint64>(file.pos());

    const QMimeData *mimeData = item.getMimeData();
    QScopedPointer<QMimeData> fallbackMime;
    if (!mimeData || !hasMeaningfulMimeData(mimeData)) {
        const QString fallbackText = item.getNormalizedText();
        const QList<QUrl> fallbackUrls = item.getNormalizedUrls();
        if (!fallbackText.isEmpty() || !fallbackUrls.isEmpty()) {
            fallbackMime.reset(new QMimeData);
            if (!fallbackText.isEmpty()) fallbackMime->setText(fallbackText);
            if (!fallbackUrls.isEmpty()) fallbackMime->setUrls(fallbackUrls);
            mimeData = fallbackMime.data();
        }
    }
    if (mimeData) {
        const QStringList formats = mimeData->formats();
        out << quint32(formats.size());
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

    if (!file.seek(mimeDataOffsetPos)) {
        qWarning() << "[LocalSaver] saveToFile failed: path=" << filePath
                    << "reason=seek failed, offset=" << mimeDataOffsetPos
                    << "fileSize=" << file.size();
    }
    out << mimeDataOffset;
    file.close();
    if (out.status() != QDataStream::Ok) {
        qWarning() << "[LocalSaver] saveToFile failed: path=" << filePath
                    << "reason=stream error, status=" << out.status();
        return false;
    }
    return true;
}

namespace {
bool rewriteCurrentFormatHeader(const QString &filePath,
                                const QString *aliasOverride,
                                const bool *pinnedOverride,
                                const QPixmap *thumbnailOverride,
                                const QDateTime *timeOverride = nullptr,
                                const QString *nameOverride = nullptr) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[LocalSaver] rewriteCurrentFormatHeader failed: path=" << filePath
                    << "reason=cannot open for reading";
        return false;
    }

    QDataStream in(&file);
    QString magic;
    quint32 version = 0;
    quint32 flags = 0;
    in >> magic >> version >> flags;
    const bool isCurrent = (magic == kLocalSaverMagicV6 && version == kLocalSaverVersionV6)
        || (magic == kLocalSaverMagicV5 && version == kLocalSaverVersionV5)
        || (magic == kLocalSaverMagicV4 && version == kLocalSaverVersionV4);
    if (!isCurrent || in.status() != QDataStream::Ok) {
        qWarning() << "[LocalSaver] rewriteCurrentFormatHeader failed: path=" << filePath
                    << "reason=invalid format or stream error, status=" << in.status();
        return false;
    }
    Q_UNUSED(flags);

    CurrentFormatHeader header;
    if (!readCurrentFormatHeader(in, version, header)) {
        return false;
    }

    if (aliasOverride) {
        header.alias = *aliasOverride;
    }
    if (pinnedOverride) {
        header.pinned = *pinnedOverride;
    }
    if (thumbnailOverride) {
        header.thumbnail = *thumbnailOverride;
    }
    if (timeOverride && timeOverride->isValid()) {
        header.time = *timeOverride;
    }
    if (nameOverride && !nameOverride->isEmpty()) {
        header.name = *nameOverride;
    }

    const qint64 fileSize = file.size();
    const bool hasBlob = header.mimeDataOffset > 0 && header.mimeDataOffset < static_cast<quint64>(fileSize);
    bool blobHasPayload = false;
    if (hasBlob) {
        if (!file.seek(static_cast<qint64>(header.mimeDataOffset))) {
            qWarning() << "[LocalSaver] rewriteCurrentFormatHeader failed: path=" << filePath
                        << "reason=seek to MIME offset failed, offset=" << header.mimeDataOffset
                        << "fileSize=" << fileSize;
            return false;
        }
        QDataStream blobIn(&file);
        quint32 formatCount = 0;
        blobIn >> formatCount;
        if (blobIn.status() != QDataStream::Ok) {
            qWarning() << "[LocalSaver] rewriteCurrentFormatHeader failed: path=" << filePath
                        << "reason=stream error reading MIME format count, status=" << blobIn.status();
            return false;
        }
        QList<QByteArray> uniqueBlobs;
        uniqueBlobs.reserve(static_cast<int>(formatCount));
        for (quint32 i = 0; i < formatCount; ++i) {
            QString format;
            quint32 dataIndex = 0;
            QByteArray data;
            blobIn >> format >> dataIndex >> data;
            if (blobIn.status() != QDataStream::Ok) {
                qWarning() << "[LocalSaver] rewriteCurrentFormatHeader failed: path=" << filePath
                            << "reason=stream error reading MIME entry, status=" << blobIn.status();
                return false;
            }
            if (!data.isEmpty()) {
                while (uniqueBlobs.size() <= static_cast<int>(dataIndex)) {
                    uniqueBlobs.append(QByteArray());
                }
                uniqueBlobs[dataIndex] = data;
                blobHasPayload = true;
            } else if (static_cast<int>(dataIndex) < uniqueBlobs.size()
                       && !uniqueBlobs[dataIndex].isEmpty()) {
                blobHasPayload = true;
            }
        }
        if (!file.seek(static_cast<qint64>(header.mimeDataOffset))) {
            qWarning() << "[LocalSaver] rewriteCurrentFormatHeader failed: path=" << filePath
                        << "reason=seek back to MIME offset failed, offset=" << header.mimeDataOffset
                        << "fileSize=" << fileSize;
            return false;
        }
    }

    QSaveFile outFile(filePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        qWarning() << "[LocalSaver] rewriteCurrentFormatHeader failed: path=" << filePath
                    << "reason=cannot open QSaveFile for writing";
        return false;
    }

    QDataStream out(&outFile);
    const qint64 offsetPos = writeCurrentFormatHeader(out, header);
    quint64 newMimeOffset = 0;
    const qint64 blobStart = outFile.pos();

    const bool shouldRepairMime = !blobHasPayload
        && (!header.normalizedText.isEmpty() || !header.normalizedUrls.isEmpty());

    if (hasBlob && !shouldRepairMime) {
        constexpr qint64 kChunkSize = 64 * 1024;
        QByteArray buffer;
        buffer.resize(static_cast<int>(kChunkSize));
        while (!file.atEnd()) {
            const qint64 read = file.read(buffer.data(), buffer.size());
            if (read <= 0) {
                break;
            }
            outFile.write(buffer.constData(), read);
        }
    } else {
        if (shouldRepairMime) {
            QByteArray urlPayload;
            if (!header.normalizedUrls.isEmpty()) {
                for (const QUrl &u : header.normalizedUrls) {
                    urlPayload.append(u.toString(QUrl::FullyEncoded).toUtf8());
                    urlPayload.append("\r\n");
                }
            }

            quint32 formatCount = 0;
            if (!header.normalizedText.isEmpty()) {
                ++formatCount;
            }
            if (!urlPayload.isEmpty()) {
                ++formatCount;
            }
            out << formatCount;
            quint32 dataIndex = 0;
            if (!header.normalizedText.isEmpty()) {
                out << QStringLiteral("text/plain;charset=utf-8") << dataIndex++ << header.normalizedText.toUtf8();
            }
            if (!urlPayload.isEmpty()) {
                out << QStringLiteral("text/uri-list") << dataIndex++ << urlPayload;
            }
        } else {
            out << quint32(0);
        }
    }

    outFile.seek(offsetPos);
    out << quint64(blobStart);

    if (out.status() != QDataStream::Ok) {
        qWarning() << "[LocalSaver] rewriteCurrentFormatHeader failed: path=" << filePath
                    << "reason=stream error after write, status=" << out.status();
        return false;
    }

    return outFile.commit();
}
}

bool LocalSaver::updateMetadata(const QString &filePath, const QString &alias, bool pinned) {
    return rewriteCurrentFormatHeader(filePath, &alias, &pinned, nullptr);
}

bool LocalSaver::updateTimestamp(const QString &filePath, const QDateTime &time, const QString &name) {
    // Lightweight header-only rewrite that doesn't parse or touch the
    // MIME blob.  This succeeds even when the MIME section is damaged.
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QDataStream in(&file);
    quint32 version = 0, flags = 0;
    QString magic;
    in >> magic >> version >> flags;
    const bool isCurrent = (magic == kLocalSaverMagicV6 && version == kLocalSaverVersionV6)
        || (magic == kLocalSaverMagicV5 && version == kLocalSaverVersionV5)
        || (magic == kLocalSaverMagicV4 && version == kLocalSaverVersionV4);
    if (!isCurrent || in.status() != QDataStream::Ok) {
        return false;
    }

    CurrentFormatHeader header;
    if (!readCurrentFormatHeader(in, version, header)) {
        return false;
    }

    // Apply overrides.
    if (time.isValid()) header.time = time;
    if (!name.isEmpty()) header.name = name;

    // Read everything from mimeDataOffset onward as raw bytes.
    const qint64 mimeStart = static_cast<qint64>(header.mimeDataOffset);
    QByteArray mimeBlob;
    if (mimeStart > 0 && mimeStart < file.size()) {
        file.seek(mimeStart);
        mimeBlob = file.readAll();
    }
    file.close();

    // Rewrite: header + raw MIME blob at the same offset.
    QSaveFile outFile(filePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        return false;
    }
    QDataStream out(&outFile);
    writeCurrentFormatHeader(out, header);

    // Patch mimeDataOffset to the actual position after the new header.
    const qint64 newMimeStart = outFile.pos();
    if (!mimeBlob.isEmpty()) {
        outFile.write(mimeBlob);
    }
    // Seek back to overwrite the mimeDataOffset field.
    // It's the last quint64 before the MIME blob.
    const qint64 offsetFieldPos = newMimeStart - static_cast<qint64>(sizeof(quint64));
    if (offsetFieldPos > 0) {
        outFile.seek(offsetFieldPos);
        QDataStream patchOut(&outFile);
        patchOut << static_cast<quint64>(newMimeStart);
    }

    if (out.status() != QDataStream::Ok) {
        return false;
    }
    return outFile.commit();
}

bool LocalSaver::updateThumbnail(const QString &filePath, const QPixmap &thumbnail) {
    return rewriteCurrentFormatHeader(filePath, nullptr, nullptr, &thumbnail);
}

ClipboardItem LocalSaver::loadFromFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[LocalSaver] loadFromFile failed: path=" << filePath
                    << "reason=cannot open for reading";
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
ClipboardItem loadFromStreamLight(QDataStream &in, const QString &filePath, bool includeThumbnail) {
    quint32 flags = 0;
    quint32 version = 0;
    if (!readCurrentFormatPrefix(in, flags, version)) {
        return ClipboardItem();
    }
    Q_UNUSED(flags);
    CurrentFormatHeader header;
    if (!readCurrentFormatHeader(in, version, header)) {
        return ClipboardItem();
    }

    // Build a light-loaded ClipboardItem (no MIME data, no full image decode).
    ClipboardItem item;
    ContentType effectiveType = static_cast<ContentType>(header.contentType);
    if (!header.thumbnail.isNull()
        && effectiveType == Text
        && header.normalizedText.trimmed().isEmpty()
        && header.normalizedUrls.isEmpty()) {
        effectiveType = Image;
    }
    {
        // Downscale icon to save memory — card header displays at ~40px.
        constexpr int kMaxIconSize = 64;
        QPixmap icon = header.icon;
        if (!icon.isNull() && (icon.width() > kMaxIconSize || icon.height() > kMaxIconSize)) {
            icon = icon.scaled(kMaxIconSize, kMaxIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        item.setIcon(icon);
    }
    item.setName(header.name);
    item.setTime(header.time);
    item.setTitle(header.title);
    item.setUrl(header.url);
    item.setAlias(header.alias);
    item.setPinned(header.pinned);
    {
        constexpr int kMaxFaviconSize = 64;
        QPixmap fav = header.favicon;
        if (!fav.isNull() && (fav.width() > kMaxFaviconSize || fav.height() > kMaxFaviconSize)) {
            fav = fav.scaled(kMaxFaviconSize, kMaxFaviconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        item.setFavicon(fav);
    }
    item.setThumbnailAvailableHint(!header.thumbnail.isNull());
    if (includeThumbnail && !header.thumbnail.isNull()) {
        QPixmap thumb = header.thumbnail;
        // QDataStream does not preserve devicePixelRatio.  The thumbnail
        // may have been saved on a screen with a different DPR than the
        // current one (e.g. synced from 1080p to 4K).  Rescale to the
        // current screen's card preview size so it always displays at
        // the correct logical dimensions.
        const qreal currentDpr = maxThumbnailDevicePixelRatio();
        const QSize targetPixelSize = QSize(kCardPreviewWidth, kCardPreviewHeight) * currentDpr;
        if (thumb.size() != targetPixelSize) {
            thumb = thumb.scaled(targetPixelSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        thumb.setDevicePixelRatio(currentDpr);
        item.setThumbnail(thumb);
    }
    item.setSourceFilePath(filePath);
    item.setMimeDataFileOffset(header.mimeDataOffset);
    item.setFingerprintCache(header.fingerprint);
    {
        const QString capturedPath = filePath;
        const quint64 capturedOffset = header.mimeDataOffset;
        item.setMimeDataLoader([capturedPath, capturedOffset](ClipboardItem &self) {
            LocalSaver::loadMimeSection(capturedPath, capturedOffset, self);
        });
    }
    item.setLightLoaded(
        effectiveType,
        header.normalizedText,
        header.normalizedUrls,
        !header.thumbnail.isNull());

    return item;
}
}

ClipboardItem LocalSaver::loadFromRawDataLight(const QByteArray &rawData,
                                               const QString &sourceFilePath,
                                               bool includeThumbnail) {
    if (rawData.isEmpty()) {
        return ClipboardItem();
    }

    QBuffer buffer;
    buffer.setData(rawData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return ClipboardItem();
    }

    QDataStream in(&buffer);
    return loadFromStreamLight(in, sourceFilePath, includeThumbnail);
}

ClipboardItem LocalSaver::loadFromFileLight(const QString &filePath, bool includeThumbnail) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[LocalSaver] loadFromFileLight failed: path=" << filePath
                    << "reason=cannot open for reading";
        return ClipboardItem();
    }

    QDataStream in(&file);
    ClipboardItem item = loadFromStreamLight(in, filePath, includeThumbnail);
    file.close();

    // The file may have been renamed (e.g. moveItemToFirst) while the
    // header still contains the old name.  Use the filename as the
    // canonical name so sorting by filename stays consistent.
    const QString fileBaseName = QFileInfo(filePath).completeBaseName();
    if (!item.getName().isEmpty() && item.getName() != fileBaseName) {
        item.setName(fileBaseName);
    }
    return item;
}

bool LocalSaver::loadMimeSection(const QString &filePath, quint64 offset, ClipboardItem &item) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[LocalSaver] loadMimeSection failed: path=" << filePath
                    << "reason=cannot open for reading";
        return false;
    }

    if (!file.seek(static_cast<qint64>(offset))) {
        qWarning() << "[LocalSaver] loadMimeSection failed: path=" << filePath
                    << "reason=seek failed, offset=" << offset
                    << "fileSize=" << file.size();
        file.close();
        return false;
    }

    QDataStream in(&file);
    quint32 formatCount = 0;
    in >> formatCount;
    if (in.status() != QDataStream::Ok) {
        qWarning() << "[LocalSaver] loadMimeSection failed: path=" << filePath
                    << "reason=stream error reading format count, status=" << in.status();
        file.close();
        return false;
    }

    // Read MIME formats with dedup reconstruction.
    QList<QByteArray> uniqueBlobs;
    const QString savedNormalizedText = item.getNormalizedText();
    const QList<QUrl> savedNormalizedUrls = item.getNormalizedUrls();

    auto *mimeData = new QMimeData;
    for (quint32 i = 0; i < formatCount; ++i) {
        QString format;
        quint32 dataIndex = 0;
        QByteArray data;
        in >> format >> dataIndex >> data;
        if (in.status() != QDataStream::Ok) {
            qWarning() << "[LocalSaver] loadMimeSection failed: path=" << filePath
                        << "reason=stream error reading MIME entry, status=" << in.status();
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

    if (!hasMeaningfulMimeData(mimeData)) {
        if (!savedNormalizedText.isEmpty()) {
            mimeData->setText(savedNormalizedText);
        }
        if (!savedNormalizedUrls.isEmpty()) {
            mimeData->setUrls(savedNormalizedUrls);
        }
    }

    // Save metadata that would be overwritten by operator=.
    const QString savedName = item.getName();
    const QDateTime savedTime = item.getTime();
    const QString savedTitle = item.getTitle();
    const QString savedUrl = item.getUrl();
    const QString savedAlias = item.getAlias();
    const bool savedPinned = item.isPinned();
    const QPixmap savedFavicon = item.getFavicon();
    const QPixmap savedThumbnail = item.thumbnail();
    const QByteArray savedFingerprint = item.fingerprint();
    const QString savedSourceFilePath = item.sourceFilePath();
    const quint64 savedMimeOffset = item.mimeDataFileOffset();

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
    item.setAlias(savedAlias);
    item.setPinned(savedPinned);
    item.setFavicon(savedFavicon);
    item.setThumbnail(savedThumbnail);
    item.setFingerprintCache(savedFingerprint);
    item.setSourceFilePath(savedSourceFilePath);
    item.setMimeDataFileOffset(savedMimeOffset);
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
        qWarning() << "[LocalSaver] loadMimePayloads failed: path=" << filePath
                    << "reason=cannot open for reading";
        return false;
    }

    if (!file.seek(static_cast<qint64>(offset))) {
        qWarning() << "[LocalSaver] loadMimePayloads failed: path=" << filePath
                    << "reason=seek failed, offset=" << offset
                    << "fileSize=" << file.size();
        file.close();
        return false;
    }

    QDataStream in(&file);
    quint32 formatCount = 0;
    in >> formatCount;
    if (in.status() != QDataStream::Ok) {
        qWarning() << "[LocalSaver] loadMimePayloads failed: path=" << filePath
                    << "reason=stream error reading format count, status=" << in.status();
        file.close();
        return false;
    }

    constexpr quint32 kMaxPreviewHtmlBytes = 2 * 1024 * 1024;
    constexpr quint32 kMaxPreviewImageBytes = 12 * 1024 * 1024;
    QList<QByteArray> uniqueBlobs;
    QByteArray htmlBytes;
    QByteArray imageBytes;

    for (quint32 i = 0; i < formatCount; ++i) {
        QString format;
        quint32 dataIndex = 0;
        in >> format >> dataIndex;
        if (in.status() != QDataStream::Ok) {
            qWarning() << "[LocalSaver] loadMimePayloads failed: path=" << filePath
                        << "reason=stream error reading MIME entry, status=" << in.status();
            file.close();
            return false;
        }

        while (uniqueBlobs.size() <= static_cast<int>(dataIndex)) {
            uniqueBlobs.append(QByteArray());
        }

        const bool wantsHtml = htmlOut && htmlBytes.isEmpty() && format == QStringLiteral("text/html");
        // Match standard image MIME types as well as Windows clipboard
        // wrappers like application/x-qt-windows-mime;value="PNG".
        bool isImageFormat = false;
        if (imageOut && imageBytes.isEmpty()) {
            isImageFormat = ContentClassifier::preferredImageFormats().contains(format)
                || format.startsWith(QStringLiteral("image/"));
            if (!isImageFormat && format.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))) {
                static const QStringList winImageValues = {
                    QStringLiteral("PNG"), QStringLiteral("JFIF"),
                    QStringLiteral("GIF"), QStringLiteral("DIB"),
                    QStringLiteral("Bitmap"),
                };
                for (const QString &val : winImageValues) {
                    if (format.contains(val)) {
                        isImageFormat = true;
                        break;
                    }
                }
            }
        }
        const bool wantsImage = isImageFormat;
        const bool shouldRead = wantsHtml || wantsImage;

        QByteArray data;
        const quint32 maxBytes = wantsHtml ? kMaxPreviewHtmlBytes
            : (wantsImage ? kMaxPreviewImageBytes : 0);
        if (!readByteArrayWithLimit(in, shouldRead ? maxBytes : 0, &data)) {
            qWarning() << "[LocalSaver] loadMimePayloads failed: path=" << filePath
                        << "reason=stream error reading MIME data blob, status=" << in.status();
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
    if (imageSizeCache_) {
        return *imageSizeCache_;
    }

    if (imageCache_ && !imageCache_->isNull()) {
        imageSizeCache_ = imageCache_->size();
        return *imageSizeCache_;
    }

    if (!mimeDataLoaded_) {
        imageSizeCache_ = probeImageSizeFromMimeSection(sourceFilePath_, mimeDataFileOffset_);
        return *imageSizeCache_;
    }

    QSize probed = probeImageSizeFromMimeData(mimeData_.data());
    if (!probed.isValid()) {
        const QPixmap pixmap = getImage();
        if (!pixmap.isNull()) {
            probed = pixmap.size();
        }
    }
    imageSizeCache_ = probed;
    return *imageSizeCache_;
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
