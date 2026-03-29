// input: Depends on ClipboardExportService.h, ClipboardItem accessors, and Qt MIME/image utilities.
// output: Rebuilds clipboard MIME payloads semantically so exported items remain usable across apps.
// pos: utils layer clipboard export implementation.
#include "ClipboardExportService.h"

#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QMimeData>
#include <QPixmap>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include "data/ClipboardItem.h"

namespace ClipboardExportService {
namespace {

QString joinedUrlText(const QList<QUrl> &urls) {
    QStringList lines;
    lines.reserve(urls.size());
    for (const QUrl &url : urls) {
        lines << (url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded));
    }
    return lines.join(QLatin1Char('\n'));
}

void setUtf8Text(QMimeData *mimeData, const QString &text) {
    if (!mimeData || text.isEmpty()) {
        return;
    }
    mimeData->setText(text);
    mimeData->setData(QStringLiteral("text/plain;charset=utf-8"), text.toUtf8());
}

void copyRawFormats(QMimeData *target, const QMimeData *source) {
    if (!target || !source) {
        return;
    }

    for (const QString &format : source->formats()) {
        const QByteArray data = source->data(format);
        if (!data.isEmpty()) {
            target->setData(format, data);
        }
    }
}

void materializeCanonicalImage(QMimeData *mimeData, const QPixmap &pixmap) {
    if (!mimeData || pixmap.isNull()) {
        return;
    }

    mimeData->setImageData(pixmap.toImage());

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

bool hasUsefulPayload(const QMimeData *mimeData) {
    if (!mimeData) {
        return false;
    }

    if ((mimeData->hasText() && !mimeData->text().isEmpty())
        || (mimeData->hasHtml() && !mimeData->html().isEmpty())
        || (mimeData->hasUrls() && !mimeData->urls().isEmpty())
        || mimeData->hasColor()) {
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

} // namespace

QMimeData *buildMimeData(const ClipboardItem &item) {
    if (item.getName().isEmpty()) {
        return nullptr;
    }

    const QMimeData *source = item.getMimeData();
    auto *mimeData = new QMimeData;
    copyRawFormats(mimeData, source);

    const ContentType contentType = item.getContentType();
    const QString normalizedText = item.getNormalizedText();
    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();
    const QString html = item.getHtml();
    const QColor color = item.getColor();

    switch (contentType) {
        case Text:
            setUtf8Text(mimeData, normalizedText);
            break;
        case Link:
            if (!normalizedUrls.isEmpty()) {
                mimeData->setUrls(normalizedUrls);
            }
            setUtf8Text(mimeData, normalizedText);
            if (!html.isEmpty()) {
                mimeData->setHtml(html);
            }
            break;
        case RichText:
            if (!html.isEmpty()) {
                mimeData->setHtml(html);
            }
            setUtf8Text(mimeData, normalizedText);
            if (!normalizedUrls.isEmpty()) {
                mimeData->setUrls(normalizedUrls);
            }
            break;
        case File:
            if (!normalizedUrls.isEmpty()) {
                mimeData->setUrls(normalizedUrls);
                setUtf8Text(mimeData, joinedUrlText(normalizedUrls));
            } else {
                setUtf8Text(mimeData, normalizedText);
            }
            break;
        case Image:
        case Office: {
            const QPixmap pixmap = item.getImage();
            if (!pixmap.isNull()) {
                materializeCanonicalImage(mimeData, pixmap);
            }
            if (!normalizedText.isEmpty()) {
                setUtf8Text(mimeData, normalizedText);
            }
            if (!html.isEmpty()) {
                mimeData->setHtml(html);
            }
            break;
        }
        case Color:
            if (color.isValid()) {
                mimeData->setColorData(color);
                setUtf8Text(mimeData, color.name(QColor::HexRgb));
            }
            break;
        case All:
            if (!html.isEmpty()) {
                mimeData->setHtml(html);
            }
            if (!normalizedUrls.isEmpty()) {
                mimeData->setUrls(normalizedUrls);
            }
            setUtf8Text(mimeData, normalizedText);
            if (color.isValid()) {
                mimeData->setColorData(color);
            }
            break;
    }

    if (!hasUsefulPayload(mimeData)) {
        delete mimeData;
        return nullptr;
    }
    return mimeData;
}

} // namespace ClipboardExportService
