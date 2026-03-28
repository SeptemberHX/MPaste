// input: Depends on ClipboardItem data, CardPreviewMetrics constants, and Qt painting/document APIs.
// output: Provides thumbnail and preview image generation for clipboard items.
// pos: utils layer thumbnail builder.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_THUMBNAILBUILDER_H
#define MPASTE_THUMBNAILBUILDER_H

#include <QByteArray>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QTextDocument>
#include <QUrl>

#include "data/ClipboardItem.h"

namespace ThumbnailBuilder {

QSize previewLogicalSize(int itemScale);
qreal htmlPreviewZoom(qreal devicePixelRatio);
QString richTextThumbnailStyleSheet();
QString simplifyHtmlForRendering(const QString &html);
QString richTextHtmlForThumbnail(QString html);

void configureRichTextThumbnailDocument(QTextDocument &document,
                                        const QString &html,
                                        const QString &imageSource,
                                        const QByteArray &imageBytes);

qreal maxScreenDevicePixelRatio();

bool isVeryTallImage(const QSize &size);
bool richTextHtmlHasImageContent(const QString &html);
bool isPreviewCacheManagedContent(const ClipboardItem &item);
bool usesVisualPreview(const ClipboardItem &item);

QImage trimTransparentPadding(const QImage &source, int paddingPx);
QImage scaleCropToTarget(const QImage &source, const QSize &pixelTargetSize);

QImage buildLinkPreviewImage(const QString &urlString, const QString &title, qreal targetDpr, int itemScale);
QImage buildTextPreviewImage(const QString &text, qreal targetDpr, int itemScale);
QImage buildFileListPreviewImage(const QList<QUrl> &urls, qreal targetDpr, int itemScale);
QImage buildCardThumbnailImageFromBytes(const QByteArray &imageBytes, qreal targetDpr, int itemScale);
QImage buildRichTextThumbnailImageFromHtml(const QString &html, const QByteArray &imageBytes, qreal thumbnailDpr, int itemScale);

QPixmap buildCardThumbnail(const ClipboardItem &item);
QPixmap buildRichTextThumbnail(const ClipboardItem &item);

ClipboardItem prepareItemForDisplayAndSave(const ClipboardItem &source);

// QTextDocument subclass that blocks all remote resource loading.
class OfflineTextDocument : public QTextDocument {
public:
    using QTextDocument::QTextDocument;
protected:
    QVariant loadResource(int type, const QUrl &url) override;
};

} // namespace ThumbnailBuilder

#endif // MPASTE_THUMBNAILBUILDER_H
