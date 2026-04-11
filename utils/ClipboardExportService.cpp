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

/// OLE container formats that cannot safely round-trip through
/// QMimeData (they require TYMED_ISTORAGE, not TYMED_HGLOBAL).
/// Exporting them causes Word to prefer a broken OLE path over
/// the working RTF/HTML fallback, resulting in "cannot display
/// this image" and lost formatting.
bool shouldExportRawFormat(const QString &format) {
    const QString lower = format.toLower();
    if (!lower.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\"")))
        return true; // standard MIME types are always safe

    // Block OLE container / structured-storage formats that cannot
    // safely round-trip through QMimeData (they need TYMED_ISTORAGE).
    // Without these, Word falls back to CF_HTML + RTF which together
    // preserve fonts, centering, images, and all rich formatting.
    if (lower.contains(QStringLiteral("embed source"))
        || lower.contains(QStringLiteral("embedded object"))
        || lower.contains(QStringLiteral("object descriptor"))
        || lower.contains(QStringLiteral("link source"))
        || lower.contains(QStringLiteral("ownerlink"))
        || lower.contains(QStringLiteral("native"))
        || lower.contains(QStringLiteral("objectlink"))) {
        return false;
    }
    return true;
}

void copyRawFormats(QMimeData *target, const QMimeData *source, bool skipAllHtml = false) {
    if (!target || !source) {
        return;
    }

    static const QString cfHtmlMime =
        QStringLiteral("application/x-qt-windows-mime;value=\"HTML Format\"");
    for (const QString &format : source->formats()) {
        if (!shouldExportRawFormat(format))
            continue;
        // When raw CF_HTML is present (Office source), skip ALL HTML
        // formats.  Qt's QWindowsMimeHtml generates CF_HTML from the
        // text/html fragment, losing the <head><style> block with CSS
        // classes for centering and fonts.  Without CF_HTML, Word falls
        // back to RTF which preserves all formatting via native commands.
        if (skipAllHtml && (format == QLatin1String("text/html") || format == cfHtmlMime))
            continue;
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
