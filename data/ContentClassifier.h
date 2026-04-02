// input: Depends on QMimeData, normalized text/urls, and content-type rules.
// output: Centralized clipboard content classification + traits.
// pos: Data-layer classification helper for clipboard formats.
// update: If I change, update data/README.md.
#ifndef MPASTE_CONTENTCLASSIFIER_H
#define MPASTE_CONTENTCLASSIFIER_H

#include <QBuffer>
#include <QDataStream>
#include <QImage>
#include <QMimeData>
#include <QPixmap>
#include <QRegularExpression>
#include <QStringList>
#include <QTextDocument>
#include <QUrl>
#include <QVariant>

#include <algorithm>

#include "ContentType.h"

namespace ContentClassifier {

struct ContentTraits {
    bool hasVector = false;
    bool hasOle = false;
    bool hasOleNative = false;
    bool hasExplicitOfficeApp = false;
    bool hasBitmap = false;
    bool hasImage = false;
    bool hasText = false;
    bool hasHtml = false;
    bool hasUrls = false;
    bool hasColor = false;
};

inline const QStringList &preferredImageFormats() {
    static const QStringList formats = {
        QStringLiteral("image/png"),
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/jpg"),
        QStringLiteral("image/webp"),
        QStringLiteral("image/gif"),
        QStringLiteral("image/bmp"),
        QStringLiteral("application/x-qt-image")
    };
    return formats;
}

inline const QStringList &commonImageFormats() {
    static const QStringList formats = {
        QStringLiteral("image/png"),
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/gif"),
        QStringLiteral("image/bmp"),
        QStringLiteral("application/x-qt-image")
    };
    return formats;
}

inline ContentTraits analyze(const QMimeData *mimeData) {
    ContentTraits traits;
    if (!mimeData) {
        return traits;
    }

    traits.hasText = mimeData->hasText() && !mimeData->text().isEmpty();
    traits.hasHtml = mimeData->hasHtml() && !mimeData->html().isEmpty();
    traits.hasUrls = mimeData->hasUrls() && !mimeData->urls().isEmpty();
    traits.hasColor = mimeData->hasColor();
    traits.hasImage = mimeData->hasImage();

    const QStringList formats = mimeData->formats();
    for (const QString &format : formats) {
        const QString lower = format.toLower();
        if (lower.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))) {
            if (lower.contains(QStringLiteral("enhancedmetafile"))
                || lower.contains(QStringLiteral("metafilepict"))
                || lower.contains(QStringLiteral("cf_enhmetafile"))
                || lower.contains(QStringLiteral("cf_metafilepict"))
                || lower.contains(QStringLiteral("emf"))
                || lower.contains(QStringLiteral("wmf"))) {
                traits.hasVector = true;
            }
            if (lower.contains(QStringLiteral("object descriptor"))
                || lower.contains(QStringLiteral("embedded object"))
                || lower.contains(QStringLiteral("embed source"))
                || lower.contains(QStringLiteral("link source"))
                || lower.contains(QStringLiteral("ole"))
                || lower.contains(QStringLiteral("office"))
                || lower.contains(QStringLiteral("powerpoint"))
                || lower.contains(QStringLiteral("excel"))
                || lower.contains(QStringLiteral("word"))) {
                traits.hasOle = true;
            }
            if (lower.contains(QStringLiteral("powerpoint"))
                || lower.contains(QStringLiteral("excel"))
                || lower.contains(QStringLiteral("word"))) {
                traits.hasExplicitOfficeApp = true;
            }
            if (lower.contains(QStringLiteral("object descriptor"))
                || lower.contains(QStringLiteral("embedded object"))
                || lower.contains(QStringLiteral("embed source"))
                || lower.contains(QStringLiteral("link source"))
                || lower.contains(QStringLiteral("native"))) {
                traits.hasOleNative = true;
            }
            if (lower.contains(QStringLiteral("png"))
                || lower.contains(QStringLiteral("jpeg"))
                || lower.contains(QStringLiteral("jpg"))
                || lower.contains(QStringLiteral("bmp"))
                || lower.contains(QStringLiteral("dib"))
                || lower.contains(QStringLiteral("gif"))
                || lower.contains(QStringLiteral("webp"))) {
                traits.hasBitmap = true;
            }
        } else if (lower == QStringLiteral("image/emf")
                   || lower == QStringLiteral("image/x-emf")
                   || lower == QStringLiteral("image/wmf")
                   || lower == QStringLiteral("image/x-wmf")) {
            traits.hasVector = true;
        } else if (lower.startsWith(QStringLiteral("image/"))) {
            traits.hasBitmap = true;
        }
    }

    if (traits.hasImage) {
        traits.hasBitmap = true;
    }

    return traits;
}

inline bool isImageLikeText(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    if (trimmed.size() == 1) {
        const QChar ch = trimmed.at(0);
        if (ch == QChar(0xFFFC) || ch == QChar::ReplacementCharacter) {
            return true;
        }
    }

    return false;
}

inline bool shouldSkipImageDecodeFormatName(const QString &format) {
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

inline QImage decodeQtSerializedImage(const QByteArray &bytes) {
    if (bytes.isEmpty()) {
        return {};
    }

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QDataStream stream(&buffer);
    QImage image;
    stream >> image;
    return image;
}

inline QString firstHtmlImageSource(const QString &html) {
    static const QRegularExpression srcRegex(
        QStringLiteral(R"(<img[^>]+src\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = srcRegex.match(html);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

inline bool hasFastImagePayload(const QMimeData *mimeData) {
    if (!mimeData) {
        return false;
    }

    if (mimeData->hasImage()) {
        return true;
    }

    const QStringList formats = mimeData->formats();
    for (const QString &format : formats) {
        const QString lower = format.toLower();
        if (lower.startsWith(QStringLiteral("image/"))) {
            return true;
        }
        if (lower.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))
            && (lower.contains(QStringLiteral("png"))
                || lower.contains(QStringLiteral("jpeg"))
                || lower.contains(QStringLiteral("jpg"))
                || lower.contains(QStringLiteral("bmp"))
                || lower.contains(QStringLiteral("dib"))
                || lower.contains(QStringLiteral("gif"))
                || lower.contains(QStringLiteral("webp")))) {
            return true;
        }
    }

    return false;
}

inline bool hasDecodableImage(const QMimeData *mimeData) {
    if (!mimeData) {
        return false;
    }

    if (mimeData->hasImage()) {
        return true;
    }

    const QStringList windowsFormats = mimeData->formats();
    for (const QString &format : windowsFormats) {
        if (format.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))) {
            QPixmap pixmap;
            const QByteArray data = mimeData->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                return true;
            }
        }
    }

    for (const QString &format : commonImageFormats()) {
        if (mimeData->hasFormat(format)) {
            QPixmap pixmap;
            const QByteArray data = mimeData->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                return true;
            }
        }
    }

    for (const QString &format : mimeData->formats()) {
        if (shouldSkipImageDecodeFormatName(format)) {
            continue;
        }

        QPixmap pixmap;
        const QByteArray data = mimeData->data(format);
        if (!data.isEmpty() && pixmap.loadFromData(data)) {
            return true;
        }
    }

    const QVariant imageData = mimeData->imageData();
    if (imageData.canConvert<QPixmap>()) {
        const QPixmap pixmap = qvariant_cast<QPixmap>(imageData);
        if (!pixmap.isNull()) {
            return true;
        }
    }
    if (imageData.canConvert<QImage>()) {
        const QImage image = qvariant_cast<QImage>(imageData);
        if (!image.isNull()) {
            return true;
        }
    }

    return false;
}

inline bool shouldTreatHtmlPayloadAsImageFast(const QMimeData *mimeData, const QString &normalizedText) {
    if (!mimeData || !mimeData->hasHtml()) {
        return false;
    }

    const QString lowerHtml = mimeData->html().toLower();
    if (!lowerHtml.contains(QStringLiteral("<img"))) {
        return false;
    }

    if (!isImageLikeText(normalizedText)) {
        return false;
    }

    return lowerHtml.contains(QStringLiteral("ksdocclipboard"))
        || lowerHtml.contains(QStringLiteral("kingsoft"))
        || lowerHtml.contains(QStringLiteral("from:'wps'"))
        || lowerHtml.contains(QStringLiteral("from:&quot;wps&quot;"))
        || hasFastImagePayload(mimeData);
}

inline bool shouldTreatHtmlPayloadAsImage(const QMimeData *mimeData, const QString &normalizedText) {
    if (!mimeData || !mimeData->hasHtml()) {
        return false;
    }

    const QString html = mimeData->html();
    const QString lowerHtml = html.toLower();
    if (!lowerHtml.contains(QStringLiteral("<img"))) {
        return false;
    }

    // Strip tags without QTextDocument to avoid remote resource loading.
    QString plainText = html;
    static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
    plainText.replace(tagRe, QString());
    plainText = plainText.trimmed();
    if (!isImageLikeText(normalizedText) && !isImageLikeText(plainText)) {
        return false;
    }

    return lowerHtml.contains(QStringLiteral("ksdocclipboard"))
        || lowerHtml.contains(QStringLiteral("kingsoft"))
        || lowerHtml.contains(QStringLiteral("from:'wps'"))
        || lowerHtml.contains(QStringLiteral("from:&quot;wps&quot;"))
        || hasDecodableImage(mimeData);
}

inline bool shouldTreatOfficePayloadAsType(const ContentTraits &traits) {
    if (!(traits.hasVector || traits.hasOle)) {
        return false;
    }
    if (traits.hasVector) {
        return true;
    }
    // Explicit Office app formats (PowerPoint/Excel/Word internal data)
    // are always Office, even when bitmap formats are also present.
    if (traits.hasExplicitOfficeApp) {
        return true;
    }
    if (traits.hasOleNative && !traits.hasBitmap) {
        return true;
    }
    return false;
}

enum ClassificationMode { LightClassification, FullClassification };

inline ContentType classify(const QMimeData *mimeData,
                            const QString &normalizedText,
                            const QList<QUrl> &normalizedUrls,
                            ClassificationMode mode = LightClassification,
                            bool hasDecodableImg = false) {
    if (!mimeData) {
        return Text;
    }

    const ContentTraits traits = analyze(mimeData);
    if (traits.hasColor) {
        return Color;
    }

    if (!normalizedUrls.isEmpty()) {
        bool allValid = std::all_of(normalizedUrls.begin(), normalizedUrls.end(),
            [](const QUrl &url) { return url.isValid() && !url.isRelative(); });
        if (allValid) {
            bool allFiles = std::all_of(normalizedUrls.begin(), normalizedUrls.end(),
                [](const QUrl &url) { return url.isLocalFile(); });
            if (allFiles) {
                return File;
            }
            bool allWebLinks = std::all_of(normalizedUrls.begin(), normalizedUrls.end(),
                [](const QUrl &url) {
                    const QString scheme = url.scheme().toLower();
                    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
                });
            if (allWebLinks) {
                return Link;
            }
        }
        if (!mimeData->hasHtml()) {
            return Text;
        }
    }

    if (mimeData->hasHtml()) {
        const bool htmlImageLike = (mode == LightClassification)
            ? shouldTreatHtmlPayloadAsImageFast(mimeData, normalizedText)
            : shouldTreatHtmlPayloadAsImage(mimeData, normalizedText);
        if (htmlImageLike) {
            return shouldTreatOfficePayloadAsType(traits) ? Office : Image;
        }
        QString text = normalizedText.trimmed();
        if (!text.contains(QLatin1Char('\n'))
            && (text.startsWith(QStringLiteral("http://")) || text.startsWith(QStringLiteral("https://")))) {
            return Link;
        }
        return RichText;
    }

    const bool hasImagePayload = (mode == LightClassification)
        ? hasFastImagePayload(mimeData)
        : (hasDecodableImg || hasDecodableImage(mimeData));
    if (hasImagePayload) {
        return shouldTreatOfficePayloadAsType(traits) ? Office : Image;
    }

    if (shouldTreatOfficePayloadAsType(traits)) {
        return Office;
    }

    return Text;
}

inline ContentType classifyLight(const QMimeData *mimeData,
                                 const QString &normalizedText,
                                 const QList<QUrl> &normalizedUrls) {
    return classify(mimeData, normalizedText, normalizedUrls, LightClassification);
}

inline ContentType classifyFull(const QMimeData *mimeData,
                                const QString &normalizedText,
                                const QList<QUrl> &normalizedUrls) {
    return classify(mimeData, normalizedText, normalizedUrls, FullClassification);
}

} // namespace ContentClassifier

#endif // MPASTE_CONTENTCLASSIFIER_H
