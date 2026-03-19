// input: Depends on ClipboardBoardService.h, LocalSaver, MPasteSettings, and Qt IO/threading utilities.
// output: Implements board persistence, deferred loading, thumbnail processing, and keyword search routines.
// pos: utils layer board service implementation.
// update: If I change, update this header block and my folder README.md.
// note: Thumbnail decode now accepts Qt serialized image payloads when needed.
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
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "data/ContentClassifier.h"
#include "data/LocalSaver.h"
#include "utils/MPasteSettings.h"

namespace {

qreal htmlPreviewZoom(qreal devicePixelRatio) {
    return qMax<qreal>(1.0, devicePixelRatio);
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

    constexpr int cardW = 275;
    constexpr int cardH = 218;
    qreal thumbnailDpr = 1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen) {
            thumbnailDpr = qMax(thumbnailDpr, screen->devicePixelRatio());
        }
    }
    const QSize pixelTargetSize = QSize(cardW, cardH) * thumbnailDpr;
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

    constexpr int cardW = 275;
    constexpr int cardH = 218;
    qreal thumbnailDpr = 1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen) {
            thumbnailDpr = qMax(thumbnailDpr, screen->devicePixelRatio());
        }
    }

    const QSize pixelTargetSize = QSize(cardW, cardH) * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return QPixmap();
    }

    const qreal previewZoom = htmlPreviewZoom(thumbnailDpr);
    const int leftPadding = qRound(10 * thumbnailDpr);
    const int rightPadding = qRound(10 * thumbnailDpr);
    const int topPadding = qRound(6 * thumbnailDpr);
    const int bottomPadding = qRound(2 * thumbnailDpr);
    const QSize contentSize(
        qMax(1, pixelTargetSize.width() - leftPadding - rightPadding),
        qMax(1, pixelTargetSize.height() - topPadding - bottomPadding));
    const QSizeF layoutSize(
        qMax(1.0, contentSize.width() / previewZoom),
        qMax(1.0, contentSize.height() / previewZoom));

    QTextDocument document;
    document.setDocumentMargin(0);
    document.setDefaultStyleSheet(QStringLiteral("body, p, div, ul, ol, li { margin: 0; padding: 0; }"));
    const QString imageSource = ContentClassifier::firstHtmlImageSource(html);
    const QByteArray imageBytes = item.imagePayloadBytesFast();
    if (!imageSource.isEmpty() && !imageBytes.isEmpty()) {
        QImage image;
        if (!image.loadFromData(imageBytes)) {
            image = ContentClassifier::decodeQtSerializedImage(imageBytes);
        }
        if (!image.isNull()) {
            document.addResource(QTextDocument::ImageResource, QUrl(imageSource), image);
        }
    }
    document.setHtml(html);
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

    snapshot.setDevicePixelRatio(thumbnailDpr);
    return snapshot;
}

QImage buildCardThumbnailImageFromBytes(const QByteArray &imageBytes, qreal targetDpr) {
    if (imageBytes.isEmpty()) {
        return QImage();
    }

    constexpr int cardW = 275;
    constexpr int cardH = 218;
    const QSize pixelTargetSize = QSize(cardW, cardH) * targetDpr;
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

QImage buildRichTextThumbnailImageFromHtml(const QString &html, const QByteArray &imageBytes, qreal thumbnailDpr) {
    if (html.isEmpty()) {
        return QImage();
    }

    constexpr int cardW = 275;
    constexpr int cardH = 218;
    const QSize pixelTargetSize = QSize(cardW, cardH) * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return QImage();
    }

    const qreal previewZoom = htmlPreviewZoom(thumbnailDpr);
    const int leftPadding = qRound(10 * thumbnailDpr);
    const int rightPadding = qRound(10 * thumbnailDpr);
    const int topPadding = qRound(6 * thumbnailDpr);
    const int bottomPadding = qRound(2 * thumbnailDpr);
    const QSize contentSize(
        qMax(1, pixelTargetSize.width() - leftPadding - rightPadding),
        qMax(1, pixelTargetSize.height() - topPadding - bottomPadding));
    const QSizeF layoutSize(
        qMax(1.0, contentSize.width() / previewZoom),
        qMax(1.0, contentSize.height() / previewZoom));

    QTextDocument document;
    document.setDocumentMargin(0);
    document.setDefaultStyleSheet(QStringLiteral("body, p, div, ul, ol, li { margin: 0; padding: 0; }"));
    const QString imageSource = ContentClassifier::firstHtmlImageSource(html);
    if (!imageSource.isEmpty() && !imageBytes.isEmpty()) {
        QImage image;
        if (image.loadFromData(imageBytes)) {
            document.addResource(QTextDocument::ImageResource, QUrl(imageSource), image);
        }
    }
    document.setHtml(html);
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

    snapshot.setDevicePixelRatio(thumbnailDpr);
    return snapshot;
}

ClipboardItem prepareItemForDisplayAndSave(const ClipboardItem &source) {
    ClipboardItem item(source);
    if (!item.hasThumbnail() && (item.getContentType() == ClipboardItem::Image
                                 || item.getContentType() == ClipboardItem::Office)) {
        const QPixmap thumbnail = buildCardThumbnail(item);
        if (!thumbnail.isNull()) {
            item.setThumbnail(thumbnail);
        }
    } else if (!item.hasThumbnail() && item.getContentType() == ClipboardItem::RichText) {
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

void ClipboardBoardService::refreshIndex() {
    deferredLoadActive_ = false;
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    waitForDeferredRead();
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

void ClipboardBoardService::processPendingItemAsync(const ClipboardItem &item, const QString &expectedName) {
    if (expectedName.isEmpty()) {
        return;
    }

    const ClipboardItem::ContentType contentType = item.getContentType();
    const ClipboardItem baseItem = item;
    const QByteArray imageBytes = (contentType == ClipboardItem::Image
            || contentType == ClipboardItem::Office
            || contentType == ClipboardItem::RichText)
        ? item.imagePayloadBytesFast()
        : QByteArray();
    const QString richHtml = contentType == ClipboardItem::RichText ? item.getHtml() : QString();
    const QSize imageSize = item.isMimeDataLoaded()
        && (contentType == ClipboardItem::Image || contentType == ClipboardItem::Office)
        ? item.getImagePixelSize()
        : QSize();
    const QString sourceFilePath = item.sourceFilePath();
    const quint64 mimeOffset = item.mimeDataFileOffset();
    const qreal thumbnailDpr = maxScreenDevicePixelRatio();

    QPointer<ClipboardBoardService> guard(this);
    QThread *thread = QThread::create([guard, expectedName, contentType, baseItem, imageBytes, richHtml, imageSize, sourceFilePath, mimeOffset, thumbnailDpr]() mutable {
        PendingItemProcessingResult result;
        QByteArray resolvedImageBytes = imageBytes;
        QString resolvedHtml = richHtml;
        if ((resolvedImageBytes.isEmpty() || resolvedHtml.isEmpty())
            && !sourceFilePath.isEmpty()
            && (contentType == ClipboardItem::Image
                || contentType == ClipboardItem::Office
                || contentType == ClipboardItem::RichText)) {
            QString htmlPayload;
            QByteArray imagePayload;
            LocalSaver::loadMimePayloads(sourceFilePath,
                                         mimeOffset,
                                         contentType == ClipboardItem::RichText ? &htmlPayload : nullptr,
                                         (contentType == ClipboardItem::Image
                                             || contentType == ClipboardItem::Office
                                             || contentType == ClipboardItem::RichText) ? &imagePayload : nullptr);
            if (resolvedHtml.isEmpty()) {
                resolvedHtml = htmlPayload;
            }
            if (resolvedImageBytes.isEmpty()) {
                resolvedImageBytes = imagePayload;
            }
        }

        if ((contentType == ClipboardItem::Image || contentType == ClipboardItem::Office)
            && !resolvedImageBytes.isEmpty()) {
            result.thumbnailImage = buildCardThumbnailImageFromBytes(resolvedImageBytes, thumbnailDpr);
            if (isVeryTallImage(imageSize)) {
                qInfo().noquote() << QStringLiteral("[thumb-build] stage=worker name=%1 image=%2x%3 thumbPx=%4x%5 thumbDpr=%6")
                    .arg(expectedName)
                    .arg(imageSize.width())
                    .arg(imageSize.height())
                    .arg(result.thumbnailImage.width())
                    .arg(result.thumbnailImage.height())
                    .arg(result.thumbnailImage.devicePixelRatio(), 0, 'f', 2);
            }
        } else if (contentType == ClipboardItem::RichText && !resolvedHtml.isEmpty()) {
            result.thumbnailImage = buildRichTextThumbnailImageFromHtml(resolvedHtml, resolvedImageBytes, thumbnailDpr);
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

    processingThreads_.append(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        processingThreads_.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ClipboardBoardService::startAsyncKeywordSearch(const QList<QPair<QString, quint64>> &candidates,
                                                    const QString &keyword,
                                                    quint64 token) {
    if (candidates.isEmpty() || keyword.isEmpty()) {
        return;
    }

    QPointer<ClipboardBoardService> guard(this);
    QThread *thread = QThread::create([guard, candidates, keyword, token]() {
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

    processingThreads_.append(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (keywordSearchThread_ == thread) {
            keywordSearchThread_ = nullptr;
        }
        processingThreads_.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    keywordSearchThread_ = thread;
    thread->start();
}

void ClipboardBoardService::continueDeferredLoad() {
    if (!deferredLoadActive_) {
        return;
    }

    processDeferredLoadedItems();
    if (deferredLoadedItems_.isEmpty() && !deferredLoadThread_ && !pendingLoadFilePaths_.isEmpty()) {
        scheduleDeferredLoadBatch();
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

    const int batchSize = deferredBatchSize_ > 0 ? deferredBatchSize_ : 1;
    const int count = qMin(batchSize, pendingLoadFilePaths_.size());
    QStringList batchPaths;
    batchPaths.reserve(count);
    for (int i = 0; i < count; ++i) {
        batchPaths.append(pendingLoadFilePaths_.takeFirst());
    }
    emit pendingCountChanged(pendingLoadFilePaths_.size());

    QPointer<ClipboardBoardService> guard(this);
    deferredLoadThread_ = QThread::create([guard, batchPaths]() {
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

    connect(deferredLoadThread_, &QThread::finished, this, [this]() {
        deferredLoadThread_ = nullptr;
    });
    connect(deferredLoadThread_, &QThread::finished, deferredLoadThread_, &QObject::deleteLater);
    deferredLoadThread_->start();
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
