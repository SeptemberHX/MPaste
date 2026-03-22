// input: Depends on ClipboardBoardService.h, LocalSaver, MPasteSettings, and Qt IO/threading utilities.
// output: Implements board persistence, deferred loading, thumbnail processing, and keyword search routines.
// pos: utils layer board service implementation.
// update: If I change, update this header block and my folder README.md.
// note: Thumbnail decode now accepts Qt serialized image payloads, uses shared card preview metrics, respects data-layer preview kind for rich text, trims rich-text margins, and backfills missing on-disk thumbnails on demand.
#include "ClipboardBoardService.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QPainter>
#include <QTextDocument>
#include <QTextOption>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "data/CardPreviewMetrics.h"
#include "data/ContentClassifier.h"
#include "data/LocalSaver.h"
#include "utils/MPasteSettings.h"

namespace {

qreal htmlPreviewZoom(qreal devicePixelRatio) {
    return qMax<qreal>(1.0, devicePixelRatio);
}

QSize previewLogicalSize(int itemScale) {
    const int scale = qMax(50, itemScale);
    return QSize(qMax(1, kCardPreviewWidth * scale / 100),
                 qMax(1, kCardPreviewHeight * scale / 100));
}

QString richTextThumbnailStyleSheet() {
    return QStringLiteral(
        "html, body { margin: 0 !important; padding: 0 !important; width: 100% !important; max-width: 100% !important; }"
        "body, body * {"
        " margin: 0 !important;"
        " padding: 0 !important;"
        " max-width: 100% !important;"
        " white-space: normal !important;"
        " overflow-wrap: anywhere !important;"
        " word-wrap: break-word !important;"
        " word-break: break-word !important;"
        "}"
        "table {"
        " width: 100% !important;"
        " max-width: 100% !important;"
        " table-layout: fixed !important;"
        " border-collapse: collapse !important;"
        "}"
        "tr, td, th {"
        " max-width: 100% !important;"
        " white-space: normal !important;"
        " overflow-wrap: anywhere !important;"
        " word-wrap: break-word !important;"
        " word-break: break-word !important;"
        "}"
        "a, span, font, strong, em, b, i, u, sub, sup {"
        " white-space: normal !important;"
        " overflow-wrap: anywhere !important;"
        " word-wrap: break-word !important;"
        " word-break: break-word !important;"
        "}"
        "img { max-width: 100% !important; height: auto !important; }"
        "pre, code {"
        " white-space: pre-wrap !important;"
        " overflow-wrap: anywhere !important;"
        " word-break: break-word !important;"
        "}");
}

QString richTextHtmlForThumbnail(QString html) {
    const QString styleTag = QStringLiteral("<style>%1</style>").arg(richTextThumbnailStyleSheet());
    const QRegularExpression headCloseRegex(QStringLiteral(R"(</head\s*>)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch headCloseMatch = headCloseRegex.match(html);
    if (headCloseMatch.hasMatch()) {
        html.insert(headCloseMatch.capturedStart(), styleTag);
        return html;
    }

    const QRegularExpression headOpenRegex(QStringLiteral(R"(<head[^>]*>)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch headMatch = headOpenRegex.match(html);
    if (headMatch.hasMatch()) {
        html.insert(headMatch.capturedEnd(), styleTag);
        return html;
    }

    const QRegularExpression htmlOpenRegex(QStringLiteral(R"(<html[^>]*>)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch htmlMatch = htmlOpenRegex.match(html);
    if (htmlMatch.hasMatch()) {
        html.insert(htmlMatch.capturedEnd(), QStringLiteral("<head>%1</head>").arg(styleTag));
        return html;
    }

    return QStringLiteral("<html><head>%1</head><body>%2</body></html>").arg(styleTag, html);
}

void configureRichTextThumbnailDocument(QTextDocument &document,
                                        const QString &html,
                                        const QString &imageSource,
                                        const QByteArray &imageBytes) {
    document.setDocumentMargin(0);
    QTextOption option = document.defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(option);
    document.setDefaultStyleSheet(richTextThumbnailStyleSheet());

    if (!imageSource.isEmpty() && !imageBytes.isEmpty()) {
        QImage image;
        if (!image.loadFromData(imageBytes)) {
            image = ContentClassifier::decodeQtSerializedImage(imageBytes);
        }
        if (!image.isNull()) {
            document.addResource(QTextDocument::ImageResource, QUrl(imageSource), image);
        }
    }

    document.setHtml(richTextHtmlForThumbnail(html));
}

qreal maxScreenDevicePixelRatio() {
    qreal dpr = 1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen) {
            dpr = qMax(dpr, screen->devicePixelRatio());
        }
    }
    return dpr;
}

bool isVeryTallImage(const QSize &size) {
    return size.isValid() && size.height() >= qMax(4000, size.width() * 4);
}

bool richTextHtmlHasImageContent(const QString &html) {
    return !ContentClassifier::firstHtmlImageSource(html).isEmpty()
        || html.contains(QStringLiteral("<img"), Qt::CaseInsensitive);
}

bool isPreviewCacheManagedContent(const ClipboardItem &item) {
    const ClipboardItem::ContentType type = item.getContentType();
    return type == ClipboardItem::Image
        || type == ClipboardItem::Office
        || type == ClipboardItem::RichText;
}

bool usesVisualPreview(const ClipboardItem &item) {
    const ClipboardItem::ContentType type = item.getContentType();
    return type == ClipboardItem::Image
        || type == ClipboardItem::Office
        || (type == ClipboardItem::RichText && item.getPreviewKind() == ClipboardItem::VisualPreview);
}

QImage trimTransparentPadding(const QImage &source, int paddingPx) {
    if (source.isNull()) {
        return {};
    }

    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0) {
        return image;
    }

    int left = width;
    int right = -1;
    int top = height;
    int bottom = -1;
    constexpr int kAlphaThreshold = 6;

    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (qAlpha(line[x]) > kAlphaThreshold) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
        }
    }

    if (right < left || bottom < top) {
        return image;
    }

    left = qMax(0, left - paddingPx);
    right = qMin(width - 1, right + paddingPx);
    top = qMax(0, top - paddingPx);
    bottom = qMin(height - 1, bottom + paddingPx);

    const QRect bounds(left, top, right - left + 1, bottom - top + 1);
    return image.copy(bounds);
}

QImage scaleCropToTarget(const QImage &source, const QSize &pixelTargetSize) {
    if (source.isNull() || !pixelTargetSize.isValid()) {
        return source;
    }

    QImage scaled = source.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return source;
    }

    const int x = qMax(0, (scaled.width() - pixelTargetSize.width()) / 2);
    const int y = qMax(0, (scaled.height() - pixelTargetSize.height()) / 2);
    return scaled.copy(x, y,
                       qMin(scaled.width(), pixelTargetSize.width()),
                       qMin(scaled.height(), pixelTargetSize.height()));
}

QDateTime itemTimestampForFile(const QFileInfo &info) {
    bool ok = false;
    const qint64 epochMs = info.completeBaseName().toLongLong(&ok);
    if (ok && epochMs > 0) {
        const QDateTime parsed = QDateTime::fromMSecsSinceEpoch(epochMs);
        if (parsed.isValid()) {
            return parsed;
        }
    }
    return info.lastModified();
}

bool isExpiredForCutoff(const QFileInfo &info, const QDateTime &cutoff) {
    return cutoff.isValid() && itemTimestampForFile(info) < cutoff;
}

QPixmap buildCardThumbnail(const ClipboardItem &item) {
    if (item.hasThumbnail()) {
        return item.thumbnail();
    }

    const QPixmap fullImage = item.getImage();
    if (fullImage.isNull()) {
        return QPixmap();
    }

    const QSize logicalSize = previewLogicalSize(MPasteSettings::getInst()->getItemScale());
    const qreal thumbnailDpr = maxScreenDevicePixelRatio();
    const QSize pixelTargetSize = logicalSize * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return fullImage;
    }

    QPixmap scaled = fullImage.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
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

QPixmap buildRichTextThumbnail(const ClipboardItem &item) {
    const QMimeData *mimeData = item.getMimeData();
    if (!mimeData || !mimeData->hasHtml()) {
        return QPixmap();
    }

    const QString html = mimeData->html();
    if (html.isEmpty()) {
        return QPixmap();
    }

    const QSize logicalSize = previewLogicalSize(MPasteSettings::getInst()->getItemScale());
    const qreal thumbnailDpr = maxScreenDevicePixelRatio();
    const QSize pixelTargetSize = logicalSize * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return QPixmap();
    }

    const qreal previewZoom = htmlPreviewZoom(thumbnailDpr);
    const int leftPadding = 0;
    const int rightPadding = 0;
    const int topPadding = 0;
    const int bottomPadding = 0;
    const QSize contentSize(
        qMax(1, pixelTargetSize.width() - leftPadding - rightPadding),
        qMax(1, pixelTargetSize.height() - topPadding - bottomPadding));
    const QSizeF layoutSize(
        qMax(1.0, contentSize.width() / previewZoom),
        qMax(1.0, contentSize.height() / previewZoom));

    const QString imageSource = ContentClassifier::firstHtmlImageSource(html);
    const QByteArray imageBytes = item.imagePayloadBytesFast();
    QTextDocument document;
    configureRichTextThumbnailDocument(document, html, imageSource, imageBytes);
    document.setPageSize(layoutSize);
    document.setTextWidth(layoutSize.width());

    QPixmap snapshot(pixelTargetSize);
    snapshot.fill(Qt::transparent);

    QPainter painter(&snapshot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.translate(leftPadding, topPadding);
    painter.scale(previewZoom, previewZoom);
    painter.setClipRect(QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    document.drawContents(&painter, QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    painter.end();

    if (!richTextHtmlHasImageContent(html)) {
        snapshot.setDevicePixelRatio(thumbnailDpr);
        return snapshot;
    }

    QImage trimmed = trimTransparentPadding(snapshot.toImage(), qRound(2 * thumbnailDpr));
    QImage scaled = scaleCropToTarget(trimmed, pixelTargetSize);
    QPixmap finalPixmap = QPixmap::fromImage(scaled.isNull() ? snapshot.toImage() : scaled);
    finalPixmap.setDevicePixelRatio(thumbnailDpr);
    return finalPixmap;
}

QImage buildCardThumbnailImageFromBytes(const QByteArray &imageBytes, qreal targetDpr, int itemScale) {
    if (imageBytes.isEmpty()) {
        return QImage();
    }

    const QSize logicalSize = previewLogicalSize(itemScale);
    const QSize pixelTargetSize = logicalSize * targetDpr;
    if (!pixelTargetSize.isValid()) {
        return QImage();
    }

    QBuffer buffer;
    buffer.setData(imageBytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return QImage();
    }

    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);

    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        const QSize scaledSize = sourceSize.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding);
        if (scaledSize.isValid()) {
            reader.setScaledSize(scaledSize);
        }
    }

    QImage decoded = reader.read();
    if (decoded.isNull()) {
        decoded.loadFromData(imageBytes);
    }
    if (decoded.isNull()) {
        decoded = ContentClassifier::decodeQtSerializedImage(imageBytes);
    }
    if (decoded.isNull()) {
        return QImage();
    }

    if (decoded.size() != pixelTargetSize) {
        decoded = decoded.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    if (decoded.isNull()) {
        return QImage();
    }

    const int x = qMax(0, (decoded.width() - pixelTargetSize.width()) / 2);
    const int y = qMax(0, (decoded.height() - pixelTargetSize.height()) / 2);
    QImage thumbnail = decoded.copy(x, y,
                                    qMin(decoded.width(), pixelTargetSize.width()),
                                    qMin(decoded.height(), pixelTargetSize.height()));
    thumbnail.setDevicePixelRatio(targetDpr);
    return thumbnail;
}

QImage buildRichTextThumbnailImageFromHtml(const QString &html, const QByteArray &imageBytes, qreal thumbnailDpr, int itemScale) {
    if (html.isEmpty()) {
        return QImage();
    }

    const QSize logicalSize = previewLogicalSize(itemScale);
    const QSize pixelTargetSize = logicalSize * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return QImage();
    }

    const qreal previewZoom = htmlPreviewZoom(thumbnailDpr);
    const int leftPadding = 0;
    const int rightPadding = 0;
    const int topPadding = 0;
    const int bottomPadding = 0;
    const QSize contentSize(
        qMax(1, pixelTargetSize.width() - leftPadding - rightPadding),
        qMax(1, pixelTargetSize.height() - topPadding - bottomPadding));
    const QSizeF layoutSize(
        qMax(1.0, contentSize.width() / previewZoom),
        qMax(1.0, contentSize.height() / previewZoom));

    const QString imageSource = ContentClassifier::firstHtmlImageSource(html);
    QTextDocument document;
    configureRichTextThumbnailDocument(document, html, imageSource, imageBytes);
    document.setPageSize(layoutSize);
    document.setTextWidth(layoutSize.width());

    QImage snapshot(pixelTargetSize, QImage::Format_ARGB32_Premultiplied);
    snapshot.fill(Qt::transparent);

    QPainter painter(&snapshot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.translate(leftPadding, topPadding);
    painter.scale(previewZoom, previewZoom);
    painter.setClipRect(QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    document.drawContents(&painter, QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    painter.end();

    if (!richTextHtmlHasImageContent(html)) {
        snapshot.setDevicePixelRatio(thumbnailDpr);
        return snapshot;
    }

    QImage trimmed = trimTransparentPadding(snapshot, qRound(2 * thumbnailDpr));
    QImage scaled = scaleCropToTarget(trimmed, pixelTargetSize);
    if (!scaled.isNull()) {
        scaled.setDevicePixelRatio(thumbnailDpr);
        return scaled;
    }
    snapshot.setDevicePixelRatio(thumbnailDpr);
    return snapshot;
}

ClipboardItem prepareItemForDisplayAndSave(const ClipboardItem &source) {
    ClipboardItem item(source);
    if (item.getContentType() == ClipboardItem::RichText
        && item.getPreviewKind() == ClipboardItem::TextPreview
        && item.hasThumbnail()) {
        item.setThumbnail(QPixmap());
    }

    if (!item.hasThumbnail() && (item.getContentType() == ClipboardItem::Image
                                 || item.getContentType() == ClipboardItem::Office)) {
        const QPixmap thumbnail = buildCardThumbnail(item);
        if (!thumbnail.isNull()) {
            item.setThumbnail(thumbnail);
        }
    } else if (!item.hasThumbnail()
               && item.getContentType() == ClipboardItem::RichText
               && item.getPreviewKind() == ClipboardItem::VisualPreview) {
        const QPixmap thumbnail = buildRichTextThumbnail(item);
        if (!thumbnail.isNull()) {
            item.setThumbnail(thumbnail);
        }
    }
    return item;
}

struct PendingItemProcessingResult {
    QImage thumbnailImage;
};

} // namespace

ClipboardBoardService::ClipboardBoardService(const QString &category, QObject *parent)
    : QObject(parent),
      category_(category),
      saver_(std::make_unique<LocalSaver>()) {
    deferredLoadTimer_ = new QTimer(this);
    deferredLoadTimer_->setSingleShot(true);
    connect(deferredLoadTimer_, &QTimer::timeout, this, &ClipboardBoardService::continueDeferredLoad);
}

ClipboardBoardService::~ClipboardBoardService() {
    deferredLoadActive_ = false;
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    if (indexRefreshThread_) {
        indexRefreshThread_->wait();
    }
    if (deferredLoadThread_) {
        deferredLoadThread_->wait();
    }
    if (keywordSearchThread_) {
        keywordSearchThread_->wait();
    }
    for (QThread *thread : processingThreads_) {
        if (thread) {
            thread->wait();
        }
    }
}

QThread *ClipboardBoardService::startTrackedThread(const std::function<void()> &task) {
    QThread *thread = QThread::create([task]() {
        task();
    });

    processingThreads_.append(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        processingThreads_.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return thread;
}

void ClipboardBoardService::trackExclusiveThread(QThread *thread, QThread **slot) {
    if (!thread || !slot) {
        return;
    }

    *slot = thread;
    connect(thread, &QThread::finished, this, [slot, thread]() {
        if (*slot == thread) {
            *slot = nullptr;
        }
    });
}

QString ClipboardBoardService::category() const {
    return category_;
}

QString ClipboardBoardService::saveDir() const {
    return QDir::cleanPath(MPasteSettings::getInst()->getSaveDir() + QDir::separator() + category_);
}

int ClipboardBoardService::totalItemCount() const {
    return totalItemCount_;
}

int ClipboardBoardService::pendingCount() const {
    return pendingLoadFilePaths_.size();
}

bool ClipboardBoardService::hasPendingItems() const {
    return !pendingLoadFilePaths_.isEmpty();
}

bool ClipboardBoardService::deferredLoadActive() const {
    return deferredLoadActive_;
}

void ClipboardBoardService::applyPendingFileIndex(const QStringList &filePaths,
                                                  int initialBatchSize,
                                                  int deferredBatchSize,
                                                  quint64 token) {
    if (token != asyncLoadToken_) {
        return;
    }

    pendingLoadFilePaths_ = filePaths;
    updateTotalItemCount(pendingLoadFilePaths_.size());
    emit pendingCountChanged(pendingLoadFilePaths_.size());

    if (pendingLoadFilePaths_.isEmpty()) {
        deferredLoadActive_ = false;
        emit deferredLoadCompleted();
        return;
    }

    deferredLoadActive_ = deferredBatchSize > 0;
    deferredBatchSize_ = qMax(1, deferredBatchSize);
    startRawReadBatch(initialBatchSize > 0 ? initialBatchSize
                                          : (deferredBatchSize_ > 0 ? deferredBatchSize_ : 1));
}

void ClipboardBoardService::refreshIndex() {
    deferredLoadActive_ = false;
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    waitForDeferredRead();
    waitForIndexRefresh();
    deferredLoadedItems_.clear();
    pendingLoadFilePaths_.clear();
    updateTotalItemCount(0);
    checkSaveDir();

    QDir saveDir(this->saveDir());
    const QFileInfoList fileInfos = saveDir.entryInfoList(QStringList() << "*.mpaste", QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &info : fileInfos) {
        if (LocalSaver::isCurrentFormatFile(info.filePath())) {
            pendingLoadFilePaths_.append(info.filePath());
        }
    }

    updateTotalItemCount(pendingLoadFilePaths_.size());
    trimExpiredPendingItems(MPasteSettings::getInst()->historyRetentionCutoff());
    emit pendingCountChanged(pendingLoadFilePaths_.size());
}

void ClipboardBoardService::startAsyncLoad(int initialBatchSize, int deferredBatchSize) {
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    deferredLoadActive_ = false;
    waitForDeferredRead();
    deferredLoadedItems_.clear();
    pendingLoadFilePaths_.clear();
    updateTotalItemCount(0);
    emit pendingCountChanged(0);
    checkSaveDir();

    const quint64 token = ++asyncLoadToken_;
    const QString directory = saveDir();
    QPointer<ClipboardBoardService> guard(this);
    QThread *thread = startTrackedThread([guard, directory, initialBatchSize, deferredBatchSize, token]() {
        QStringList filePaths;
        QDir dir(directory);
        const QFileInfoList fileInfos = dir.entryInfoList(QStringList() << "*.mpaste", QDir::Files, QDir::Name | QDir::Reversed);
        for (const QFileInfo &info : fileInfos) {
            if (LocalSaver::isCurrentFormatFile(info.filePath())) {
                filePaths.append(info.filePath());
            }
        }

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, filePaths, initialBatchSize, deferredBatchSize, token]() {
                if (guard) {
                    guard->applyPendingFileIndex(filePaths, initialBatchSize, deferredBatchSize, token);
                }
            }, Qt::QueuedConnection);
        }
    });
    trackExclusiveThread(thread, &indexRefreshThread_);
}

void ClipboardBoardService::loadNextBatch(int batchSize) {
    if (batchSize <= 0 || pendingLoadFilePaths_.isEmpty()) {
        return;
    }

    QList<QPair<QString, ClipboardItem>> loadedItems;
    const int count = qMin(batchSize, pendingLoadFilePaths_.size());
    for (int i = 0; i < count; ++i) {
        const QString filePath = pendingLoadFilePaths_.takeFirst();
        ClipboardItem item = saver_->loadFromFileLight(filePath);
        if (item.getName().isEmpty()) {
            deleteItemByPath(filePath);
            continue;
        }
        loadedItems.append(qMakePair(filePath, item));
    }

    emit pendingCountChanged(pendingLoadFilePaths_.size());
    if (!loadedItems.isEmpty()) {
        emit itemsLoaded(loadedItems);
    }
}

void ClipboardBoardService::ensureAllItemsLoaded(int batchSize) {
    deferredLoadActive_ = false;
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    waitForDeferredRead();
    while (!deferredLoadedItems_.isEmpty()) {
        processDeferredLoadedItems();
    }
    while (!pendingLoadFilePaths_.isEmpty()) {
        loadNextBatch(batchSize);
    }
}

void ClipboardBoardService::startDeferredLoad(int batchSize) {
    deferredLoadActive_ = true;
    deferredBatchSize_ = qMax(1, batchSize);
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    scheduleDeferredLoadBatch();
}

void ClipboardBoardService::stopDeferredLoad() {
    deferredLoadActive_ = false;
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
}

void ClipboardBoardService::setVisibleHint(bool visible) {
    visibleHint_ = visible;
}

ClipboardItem ClipboardBoardService::prepareItemForSave(const ClipboardItem &source) const {
    return prepareItemForDisplayAndSave(source);
}

void ClipboardBoardService::saveItem(const ClipboardItem &item) {
    checkSaveDir();
    saver_->saveToFile(item, filePathForItem(item));
    emit localPersistenceChanged();
}

void ClipboardBoardService::removeItemFile(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }
    saver_->removeItem(filePath);
    emit localPersistenceChanged();
}

void ClipboardBoardService::deleteItemByPath(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    saver_->removeItem(filePath);
    emit localPersistenceChanged();
    const int pendingIndex = pendingLoadFilePaths_.indexOf(filePath);
    if (pendingIndex >= 0) {
        pendingLoadFilePaths_.removeAt(pendingIndex);
        emit pendingCountChanged(pendingLoadFilePaths_.size());
    }
    decrementTotalItemCount();
}

bool ClipboardBoardService::deletePendingItemByPath(const QString &filePath) {
    if (filePath.isEmpty()) {
        return false;
    }

    const int pendingIndex = pendingLoadFilePaths_.indexOf(filePath);
    if (pendingIndex < 0) {
        return false;
    }

    pendingLoadFilePaths_.removeAt(pendingIndex);
    saver_->removeItem(filePath);
    emit localPersistenceChanged();
    emit pendingCountChanged(pendingLoadFilePaths_.size());
    decrementTotalItemCount();
    return true;
}

QString ClipboardBoardService::filePathForItem(const ClipboardItem &item) const {
    return filePathForName(item.getName());
}

QString ClipboardBoardService::filePathForName(const QString &name) const {
    if (name.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(saveDir() + QDir::separator() + name + ".mpaste");
}

ClipboardItem ClipboardBoardService::loadItemLight(const QString &filePath) {
    return saver_->loadFromFileLight(filePath);
}

QSet<QByteArray> ClipboardBoardService::loadAllFingerprints() {
    checkSaveDir();

    QSet<QByteArray> fingerprints;
    QDir dir(saveDir());
    const QFileInfoList fileInfos = dir.entryInfoList(QStringList() << "*.mpaste", QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &info : fileInfos) {
        if (!LocalSaver::isCurrentFormatFile(info.filePath())) {
            continue;
        }
        const ClipboardItem item = saver_->loadFromFileLight(info.filePath());
        if (!item.getName().isEmpty()) {
            fingerprints.insert(item.fingerprint());
        }
    }
    return fingerprints;
}

void ClipboardBoardService::notifyItemAdded() {
    updateTotalItemCount(totalItemCount_ + 1);
}

void ClipboardBoardService::trimExpiredPendingItems(const QDateTime &cutoff) {
    if (category_ == MPasteSettings::STAR_CATEGORY_NAME) {
        return;
    }

    while (!pendingLoadFilePaths_.isEmpty()) {
        const QFileInfo info(pendingLoadFilePaths_.last());
        if (!isExpiredForCutoff(info, cutoff)) {
            break;
        }

        deleteItemByPath(pendingLoadFilePaths_.last());
    }
}

int ClipboardBoardService::maintainPreviewCache(PreviewCacheMaintenanceMode mode) {
    checkSaveDir();

    QDir dir(saveDir());
    const QFileInfoList fileInfos = dir.entryInfoList(QStringList() << "*.mpaste", QDir::Files, QDir::Name);
    if (fileInfos.isEmpty()) {
        return 0;
    }

    LocalSaver saver;
    int changedCount = 0;

    for (const QFileInfo &info : fileInfos) {
        const QString filePath = info.filePath();
        if (!LocalSaver::isCurrentFormatFile(filePath)) {
            continue;
        }

        const ClipboardItem lightItem = saver.loadFromFileLight(filePath);
        if (lightItem.getName().isEmpty() || !isPreviewCacheManagedContent(lightItem)) {
            continue;
        }

        switch (mode) {
            case ClearAllPreviews: {
                const ClipboardItem fullItem = saver.loadFromFile(filePath);
                if (fullItem.getName().isEmpty() || !fullItem.hasThumbnail()) {
                    break;
                }
                if (saver.updateThumbnail(filePath, QPixmap())) {
                    ++changedCount;
                }
                break;
            }
            case RepairBrokenPreviews: {
                if (lightItem.getContentType() == ClipboardItem::RichText
                    && lightItem.getPreviewKind() == ClipboardItem::TextPreview) {
                    const ClipboardItem fullItem = saver.loadFromFile(filePath);
                    if (!fullItem.getName().isEmpty()
                        && fullItem.hasThumbnail()
                        && saver.updateThumbnail(filePath, QPixmap())) {
                        ++changedCount;
                    }
                    break;
                }

                if (!usesVisualPreview(lightItem) || lightItem.hasThumbnail()) {
                    break;
                }

                const ClipboardItem fullItem = saver.loadFromFile(filePath);
                if (fullItem.getName().isEmpty()) {
                    break;
                }

                const ClipboardItem preparedItem = prepareItemForDisplayAndSave(fullItem);
                if (!preparedItem.thumbnail().isNull()
                    && saver.updateThumbnail(filePath, preparedItem.thumbnail())) {
                    ++changedCount;
                }
                break;
            }
            case RebuildAllPreviews: {
                const ClipboardItem fullItem = saver.loadFromFile(filePath);
                if (fullItem.getName().isEmpty()) {
                    break;
                }

                const ClipboardItem preparedItem = prepareItemForDisplayAndSave(fullItem);
                if (preparedItem.thumbnail().cacheKey() == fullItem.thumbnail().cacheKey()) {
                    break;
                }
                if (saver.updateThumbnail(filePath, preparedItem.thumbnail())) {
                    ++changedCount;
                }
                break;
            }
        }
    }

    if (changedCount > 0) {
        emit localPersistenceChanged();
    }
    return changedCount;
}

void ClipboardBoardService::processPendingItemAsync(const ClipboardItem &item, const QString &expectedName) {
    if (expectedName.isEmpty()) {
        return;
    }

    const ClipboardItem::ContentType contentType = item.getContentType();
    const ClipboardItem::PreviewKind previewKind = item.getPreviewKind();
    const ClipboardItem baseItem = item;
    const QByteArray imageBytes = (contentType == ClipboardItem::Image
            || contentType == ClipboardItem::Office
            || (contentType == ClipboardItem::RichText && previewKind == ClipboardItem::VisualPreview))
        ? item.imagePayloadBytesFast()
        : QByteArray();
    const QString richHtml = (contentType == ClipboardItem::RichText && previewKind == ClipboardItem::VisualPreview)
        ? item.getHtml()
        : QString();
    const QSize imageSize = item.isMimeDataLoaded()
        && (contentType == ClipboardItem::Image || contentType == ClipboardItem::Office)
        ? item.getImagePixelSize()
        : QSize();
    const QString sourceFilePath = item.sourceFilePath();
    const quint64 mimeOffset = item.mimeDataFileOffset();
    const qreal thumbnailDpr = maxScreenDevicePixelRatio();
    const int itemScale = MPasteSettings::getInst()->getItemScale();

    QPointer<ClipboardBoardService> guard(this);
    startTrackedThread([guard, expectedName, contentType, previewKind, baseItem, imageBytes, richHtml, imageSize, sourceFilePath, mimeOffset, thumbnailDpr, itemScale]() mutable {
        PendingItemProcessingResult result;
        QByteArray resolvedImageBytes = imageBytes;
        QString resolvedHtml = richHtml;
        if ((resolvedImageBytes.isEmpty() || resolvedHtml.isEmpty())
            && !sourceFilePath.isEmpty()
            && (contentType == ClipboardItem::Image
                || contentType == ClipboardItem::Office
                || (contentType == ClipboardItem::RichText && previewKind == ClipboardItem::VisualPreview))) {
            QString htmlPayload;
            QByteArray imagePayload;
            LocalSaver::loadMimePayloads(sourceFilePath,
                                         mimeOffset,
                                         (contentType == ClipboardItem::RichText && previewKind == ClipboardItem::VisualPreview) ? &htmlPayload : nullptr,
                                         (contentType == ClipboardItem::Image
                                            || contentType == ClipboardItem::Office
                                            || (contentType == ClipboardItem::RichText && previewKind == ClipboardItem::VisualPreview)) ? &imagePayload : nullptr);
            if (resolvedHtml.isEmpty()) {
                resolvedHtml = htmlPayload;
            }
            if (resolvedImageBytes.isEmpty()) {
                resolvedImageBytes = imagePayload;
            }
        }

        if ((contentType == ClipboardItem::Image || contentType == ClipboardItem::Office)
            && !resolvedImageBytes.isEmpty()) {
            result.thumbnailImage = buildCardThumbnailImageFromBytes(resolvedImageBytes, thumbnailDpr, itemScale);
            if (isVeryTallImage(imageSize)) {
                qInfo().noquote() << QStringLiteral("[thumb-build] stage=worker name=%1 image=%2x%3 thumbPx=%4x%5 thumbDpr=%6")
                    .arg(expectedName)
                    .arg(imageSize.width())
                    .arg(imageSize.height())
                    .arg(result.thumbnailImage.width())
                    .arg(result.thumbnailImage.height())
                    .arg(result.thumbnailImage.devicePixelRatio(), 0, 'f', 2);
            }
        } else if (contentType == ClipboardItem::RichText
                   && previewKind == ClipboardItem::VisualPreview
                   && !resolvedHtml.isEmpty()) {
            result.thumbnailImage = buildRichTextThumbnailImageFromHtml(resolvedHtml, resolvedImageBytes, thumbnailDpr, itemScale);
        }

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, expectedName, baseItem, result, thumbnailDpr]() mutable {
                if (!guard) {
                    return;
                }

                ClipboardItem preparedItem = baseItem;
                if (preparedItem.getName().isEmpty()) {
                    return;
                }

                if (!result.thumbnailImage.isNull()) {
                    QPixmap thumbnail = QPixmap::fromImage(result.thumbnailImage);
                    thumbnail.setDevicePixelRatio(qMax<qreal>(1.0, thumbnailDpr));
                    preparedItem.setThumbnail(thumbnail);
                    const QSize imageSize = preparedItem.getImagePixelSize();
                    if (isVeryTallImage(imageSize)) {
                        qInfo().noquote() << QStringLiteral("[thumb-build] stage=ui name=%1 image=%2x%3 thumbPx=%4x%5 thumbLogical=%6x%7 thumbDpr=%8")
                            .arg(expectedName)
                            .arg(imageSize.width())
                            .arg(imageSize.height())
                            .arg(thumbnail.width())
                            .arg(thumbnail.height())
                            .arg(qRound(thumbnail.width() / qMax<qreal>(1.0, thumbnail.devicePixelRatio())))
                            .arg(qRound(thumbnail.height() / qMax<qreal>(1.0, thumbnail.devicePixelRatio())))
                            .arg(thumbnail.devicePixelRatio(), 0, 'f', 2);
                    }
                }

                guard->saveItem(preparedItem);

                ClipboardItem reloadedItem = guard->loadItemLight(guard->filePathForName(expectedName));
                if (reloadedItem.getName().isEmpty() || reloadedItem.getName() != expectedName) {
                    return;
                }

                emit guard->pendingItemReady(expectedName, reloadedItem);
            }, Qt::QueuedConnection);
        }
    });
}

void ClipboardBoardService::requestThumbnailAsync(const QString &expectedName, const QString &filePath) {
    if (expectedName.isEmpty() || filePath.isEmpty()) {
        return;
    }

    QPointer<ClipboardBoardService> guard(this);
    startTrackedThread([guard, expectedName, filePath]() mutable {
        LocalSaver saver;
        ClipboardItem loaded = saver.loadFromFileLight(filePath);
        ClipboardItem preparedItem = loaded;
        bool generatedThumbnail = false;
        bool refreshedRichText = false;
        const QString loadedNormalizedText = loaded.getNormalizedText();
        if (!loaded.getName().isEmpty()) {
            const ClipboardItem::ContentType type = loaded.getContentType();
            const bool shouldRebuild = (type == ClipboardItem::RichText && loaded.getPreviewKind() == ClipboardItem::VisualPreview)
                || ((type == ClipboardItem::Image || type == ClipboardItem::Office) && loaded.thumbnail().isNull());
            if (shouldRebuild) {
                ClipboardItem fullItem = saver.loadFromFile(filePath);
                if (!fullItem.getName().isEmpty()) {
                    preparedItem = prepareItemForDisplayAndSave(fullItem);
                    generatedThumbnail = !preparedItem.thumbnail().isNull()
                        && preparedItem.thumbnail().cacheKey() != loaded.thumbnail().cacheKey();
                    refreshedRichText = type == ClipboardItem::RichText;
                }
            }
        }
        const QPixmap thumbnail = preparedItem.thumbnail();
        const bool shouldPersistPreparedItem = generatedThumbnail
            || (refreshedRichText && preparedItem.getNormalizedText() != loadedNormalizedText);

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, expectedName, preparedItem, thumbnail, generatedThumbnail, refreshedRichText, shouldPersistPreparedItem]() {
                if (!guard) {
                    return;
                }
                if ((generatedThumbnail || refreshedRichText) && !preparedItem.getName().isEmpty()) {
                    if (shouldPersistPreparedItem) {
                        guard->saveItem(preparedItem);
                        ClipboardItem reloaded = guard->loadItemLight(guard->filePathForName(expectedName));
                        if (!reloaded.getName().isEmpty()) {
                            emit guard->pendingItemReady(expectedName, reloaded);
                            return;
                        }
                    }
                    emit guard->pendingItemReady(expectedName, preparedItem);
                    return;
                }
                if (!thumbnail.isNull()) {
                    emit guard->thumbnailReady(expectedName, thumbnail);
                } else {
                    ClipboardItem reloaded = guard->loadItemLight(guard->filePathForName(expectedName));
                    if (!reloaded.getName().isEmpty()) {
                        emit guard->pendingItemReady(expectedName, reloaded);
                        return;
                    }
                    emit guard->thumbnailReady(expectedName, thumbnail);
                }
            }, Qt::QueuedConnection);
        }
    });
}

void ClipboardBoardService::startAsyncKeywordSearch(const QList<QPair<QString, quint64>> &candidates,
                                                    const QString &keyword,
                                                    quint64 token) {
    if (candidates.isEmpty() || keyword.isEmpty()) {
        return;
    }

    QPointer<ClipboardBoardService> guard(this);
    QThread *thread = startTrackedThread([guard, candidates, keyword, token]() {
        QSet<QString> matchedNames;
        for (const auto &candidate : candidates) {
            if (candidate.first.isEmpty()) {
                continue;
            }
            if (LocalSaver::mimeSectionContainsKeyword(candidate.first, candidate.second, keyword)) {
                const QFileInfo info(candidate.first);
                matchedNames.insert(info.completeBaseName());
            }
        }

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, matchedNames, token]() {
                if (!guard) {
                    return;
                }
                emit guard->keywordMatched(matchedNames, token);
            }, Qt::QueuedConnection);
        }
    });
    trackExclusiveThread(thread, &keywordSearchThread_);
}

void ClipboardBoardService::continueDeferredLoad() {
    if (!deferredLoadActive_) {
        return;
    }

    processDeferredLoadedItems();
    if (deferredLoadedItems_.isEmpty() && !deferredLoadThread_ && !pendingLoadFilePaths_.isEmpty()) {
        startRawReadBatch(deferredBatchSize_);
    }

    if (deferredLoadedItems_.isEmpty() && !deferredLoadThread_ && pendingLoadFilePaths_.isEmpty()) {
        deferredLoadActive_ = false;
        emit deferredLoadCompleted();
    }
}

void ClipboardBoardService::handleDeferredBatchRead(const QList<QPair<QString, QByteArray>> &batchPayloads) {
    if (!batchPayloads.isEmpty()) {
        deferredLoadedItems_.append(batchPayloads);
    }

    if (deferredLoadTimer_ && !deferredLoadTimer_->isActive()) {
        deferredLoadTimer_->start(visibleHint_ ? 0 : 8);
    }

    if (deferredLoadActive_ && !pendingLoadFilePaths_.isEmpty()) {
        scheduleDeferredLoadBatch();
    }
}

void ClipboardBoardService::checkSaveDir() {
    QDir dir;
    const QString path = QDir::cleanPath(saveDir());
    if (!dir.exists(path)) {
        dir.mkpath(path);
    }
}

void ClipboardBoardService::updateTotalItemCount(int total) {
    if (total == totalItemCount_) {
        return;
    }
    totalItemCount_ = qMax(0, total);
    emit totalItemCountChanged(totalItemCount_);
}

void ClipboardBoardService::decrementTotalItemCount() {
    if (totalItemCount_ <= 0) {
        return;
    }
    updateTotalItemCount(totalItemCount_ - 1);
}

void ClipboardBoardService::scheduleDeferredLoadBatch() {
    if (!deferredLoadActive_ || deferredLoadThread_ || pendingLoadFilePaths_.isEmpty()) {
        return;
    }

    startRawReadBatch(deferredBatchSize_);
}

void ClipboardBoardService::startRawReadBatch(int batchSize) {
    if (batchSize <= 0 || deferredLoadThread_ || pendingLoadFilePaths_.isEmpty()) {
        return;
    }

    const int count = qMin(batchSize, pendingLoadFilePaths_.size());
    QStringList batchPaths;
    batchPaths.reserve(count);
    for (int i = 0; i < count; ++i) {
        batchPaths.append(pendingLoadFilePaths_.takeFirst());
    }
    emit pendingCountChanged(pendingLoadFilePaths_.size());

    QPointer<ClipboardBoardService> guard(this);
    QThread *thread = startTrackedThread([guard, batchPaths]() {
        QList<QPair<QString, QByteArray>> batchPayloads;
        batchPayloads.reserve(batchPaths.size());
        for (const QString &filePath : batchPaths) {
            QByteArray rawData;
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly)) {
                rawData = file.readAll();
                file.close();
            }
            batchPayloads.append(qMakePair(filePath, rawData));
        }
        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, batchPayloads]() {
                if (guard) {
                    guard->handleDeferredBatchRead(batchPayloads);
                }
            }, Qt::QueuedConnection);
        }
    });
    trackExclusiveThread(thread, &deferredLoadThread_);
}

void ClipboardBoardService::processDeferredLoadedItems() {
    if (deferredLoadedItems_.isEmpty()) {
        return;
    }

    QElapsedTimer parseTimer;
    parseTimer.start();
    const bool widgetVisible = visibleHint_;
    const int maxItemsPerTick = widgetVisible ? 2 : 1;
    const int maxParseMs = widgetVisible ? 12 : 4;
    int processedCount = 0;
    QList<QPair<QString, ClipboardItem>> loadedItems;

    while (!deferredLoadedItems_.isEmpty() && processedCount < maxItemsPerTick && parseTimer.elapsed() < maxParseMs) {
        const auto payload = deferredLoadedItems_.takeFirst();
        const QString filePath = payload.first;
        const QByteArray rawData = payload.second;
        ClipboardItem item = rawData.isEmpty() ? ClipboardItem() : saver_->loadFromRawDataLight(rawData, filePath);
        if (item.getName().isEmpty()) {
            deleteItemByPath(filePath);
        } else {
            loadedItems.append(qMakePair(filePath, item));
        }
        ++processedCount;
    }

    if (!loadedItems.isEmpty()) {
        emit itemsLoaded(loadedItems);
    }

    if (!deferredLoadedItems_.isEmpty() && deferredLoadTimer_) {
        deferredLoadTimer_->start(widgetVisible ? 0 : 8);
    }
}

void ClipboardBoardService::waitForDeferredRead() {
    if (deferredLoadThread_) {
        deferredLoadThread_->wait();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

void ClipboardBoardService::waitForIndexRefresh() {
    if (indexRefreshThread_) {
        indexRefreshThread_->wait();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}
