// input: QMimeData payloads containing image data in various formats.
// output: Decoded QPixmap images and raw image byte extraction.
// pos: Data-layer image decoding utilities extracted from ClipboardItem.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardItemImageDecoder.h"

#include <QBuffer>
#include <QImage>
#include <QMimeData>
#include <QStringList>
#include <QVariant>

#include "ContentClassifier.h"

namespace ClipboardImageDecoder {

QPixmap decodePixmapFromMimeData(const QMimeData *mimeData) {
    if (!mimeData) {
        return QPixmap();
    }

    const QStringList formats = mimeData->formats();
    for (const QString &format : formats) {
        if (format.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))) {
            QPixmap pixmap;
            const QByteArray data = mimeData->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                return pixmap;
            }
        }
    }

    for (const QString &format : ContentClassifier::commonImageFormats()) {
        if (mimeData->hasFormat(format)) {
            QPixmap pixmap;
            const QByteArray data = mimeData->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                return pixmap;
            }
        }
    }

    for (const QString &format : formats) {
        if (ContentClassifier::shouldSkipImageDecodeFormatName(format)) {
            continue;
        }

        QPixmap pixmap;
        const QByteArray data = mimeData->data(format);
        if (!data.isEmpty() && pixmap.loadFromData(data)) {
            return pixmap;
        }
    }

    const QVariant imageData = mimeData->imageData();
    if (imageData.canConvert<QPixmap>()) {
        const QPixmap pixmap = qvariant_cast<QPixmap>(imageData);
        if (!pixmap.isNull()) {
            return pixmap;
        }
    }
    if (imageData.canConvert<QImage>()) {
        const QImage image = qvariant_cast<QImage>(imageData);
        if (!image.isNull()) {
            return QPixmap::fromImage(image);
        }
    }

    return QPixmap();
}

void materializeCanonicalImage(QMimeData *mimeData, const QPixmap &pixmap) {
    if (!mimeData || pixmap.isNull()) {
        return;
    }

    QByteArray imageData;
    QBuffer buffer(&imageData);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return;
    }
    if (!pixmap.save(&buffer, "PNG")) {
        return;
    }

    mimeData->setData(QStringLiteral("application/x-qt-image"), imageData);
    mimeData->setData(QStringLiteral("image/png"), imageData);
    mimeData->setData(QStringLiteral("application/x-qt-windows-mime;value=\"PNG\""), imageData);
}

QByteArray extractFastImagePayloadBytes(const QMimeData *mimeData, QString *formatOut) {
    if (!mimeData) {
        return {};
    }
    if (formatOut) {
        formatOut->clear();
    }

    for (const QString &format : ContentClassifier::preferredImageFormats()) {
        if (mimeData->hasFormat(format)) {
            const QByteArray data = mimeData->data(format);
            if (!data.isEmpty()) {
                if (formatOut) {
                    *formatOut = format;
                }
                return data;
            }
        }
    }

    const QStringList formats = mimeData->formats();
    for (const QString &format : formats) {
        const QString lower = format.toLower();
        const bool imageMime = lower.startsWith(QStringLiteral("image/"));
        const bool imageWindowsMime = lower.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))
            && (lower.contains(QStringLiteral("png"))
                || lower.contains(QStringLiteral("jpeg"))
                || lower.contains(QStringLiteral("jpg"))
                || lower.contains(QStringLiteral("bmp"))
                || lower.contains(QStringLiteral("dib"))
                || lower.contains(QStringLiteral("gif"))
                || lower.contains(QStringLiteral("webp")));
        if (imageMime || imageWindowsMime) {
            const QByteArray data = mimeData->data(format);
            if (!data.isEmpty()) {
                if (formatOut) {
                    *formatOut = format;
                }
                return data;
            }
        }
    }

    return {};
}

bool shouldSkipImageDecodeFormat(const QString &format) {
    return ContentClassifier::shouldSkipImageDecodeFormatName(format);
}

} // namespace ClipboardImageDecoder
