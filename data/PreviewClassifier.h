// input: Depends on ContentType, PreviewKind, ContentClassifier helpers, and lightweight/full clipboard payload hints.
// output: Centralizes preview kind decisions for cards and preview workers.
// pos: Data-layer preview policy helper shared by ClipboardItem, services, and widgets.
// update: If I change, update data/README.md.
#ifndef MPASTE_PREVIEWCLASSIFIER_H
#define MPASTE_PREVIEWCLASSIFIER_H

#include <QMimeData>
#include <QPixmap>
#include <QString>

#include "ContentClassifier.h"
#include "ContentType.h"
#include "PreviewKind.h"

namespace PreviewClassifier {

inline bool hasMeaningfulPreviewText(const QString &text) {
    const QString trimmed = text.trimmed();
    return !trimmed.isEmpty() && !ContentClassifier::isImageLikeText(trimmed);
}

inline ClipboardPreviewKind defaultPreviewKindForType(ContentType type) {
    switch (type) {
        case Image:
        case Office:
        case Color:
        case Link:
            return VisualPreview;
        case RichText:
        case Text:
        case File:
        case All:
            break;
    }
    return TextPreview;
}

inline ClipboardPreviewKind classifyLight(ContentType type,
                                          const QString &normalizedText,
                                          bool hasThumbnailHint,
                                          const QMimeData *mimeData,
                                          bool hasFastImagePayload) {
    if (type != RichText) {
        return defaultPreviewKindForType(type);
    }

    if (hasMeaningfulPreviewText(normalizedText)) {
        return TextPreview;
    }

    if (hasThumbnailHint) {
        return VisualPreview;
    }

    if (mimeData && mimeData->hasHtml()) {
        const QString html = mimeData->html();
        if (!ContentClassifier::firstHtmlImageSource(html).isEmpty()
            || html.contains(QStringLiteral("<img"), Qt::CaseInsensitive)
            || hasFastImagePayload) {
            return VisualPreview;
        }
    }

    return TextPreview;
}

inline ClipboardPreviewKind classifyFull(ContentType type,
                                         const QString &normalizedText,
                                         const QMimeData *mimeData,
                                         bool hasFastImagePayload,
                                         bool hasDecodableImage) {
    if (type != RichText) {
        return defaultPreviewKindForType(type);
    }

    if (hasMeaningfulPreviewText(normalizedText)) {
        return TextPreview;
    }

    if (mimeData && mimeData->hasHtml()) {
        const QString html = mimeData->html();
        if (!ContentClassifier::firstHtmlImageSource(html).isEmpty()
            || html.contains(QStringLiteral("<img"), Qt::CaseInsensitive)
            || hasFastImagePayload
            || hasDecodableImage) {
            return VisualPreview;
        }
    }

    return TextPreview;
}

} // namespace PreviewClassifier

#endif // MPASTE_PREVIEWCLASSIFIER_H
