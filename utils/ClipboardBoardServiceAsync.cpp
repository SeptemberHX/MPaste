// input: Depends on ClipboardBoardService.h, LocalSaver, ThumbnailBuilder, and Qt threading utilities.
// output: Implements background tasks: thread management, thumbnail processing, and async keyword search.
// pos: utils layer board service implementation (async / background tasks).
// update: If I change, update this header block and my folder README.md.
#include "ClipboardBoardService.h"
#include "ClipboardBoardServiceInternal.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QImage>
#include <QMutexLocker>
#include <QPixmap>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <QRunnable>
#include <QUrl>

#include "data/LocalSaver.h"
#include "utils/MPasteSettings.h"
#include "utils/ThumbnailBuilder.h"

namespace {

struct PendingItemProcessingResult {
    QImage thumbnailImage;
};

} // namespace

// --- Thread management ---

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

void ClipboardBoardService::startThumbnailTask(const std::function<void()> &task) {
    if (!thumbnailTaskPool_) {
        task();
        return;
    }

    thumbnailTaskPool_->start(QRunnable::create([task]() {
        task();
    }));
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

// --- Async thumbnail / rich-text processing ---

void ClipboardBoardService::processPendingItemAsync(const ClipboardItem &item, const QString &expectedName) {
    if (expectedName.isEmpty()) {
        return;
    }

    const ContentType contentType = item.getContentType();
    const ClipboardPreviewKind previewKind = item.getPreviewKind();
    const ClipboardItem baseItem = item;
    const QByteArray imageBytes = (contentType == Image
            || contentType == Office
            || (contentType == RichText && previewKind == VisualPreview))
        ? item.imagePayloadBytesFast()
        : QByteArray();
    const QString richHtml = ((contentType == RichText && previewKind == VisualPreview)
            || contentType == Office)
        ? item.getHtml()
        : QString();
    const QSize imageSize = item.isMimeDataLoaded()
        && (contentType == Image || contentType == Office)
        ? item.getImagePixelSize()
        : QSize();
    const QString sourceFilePath = item.sourceFilePath();
    const quint64 mimeOffset = item.mimeDataFileOffset();
    const qreal thumbnailDpr = ThumbnailBuilder::maxScreenDevicePixelRatio();
    const int itemScale = MPasteSettings::getInst()->getItemScale();

    QPointer<ClipboardBoardService> guard(this);
    startThumbnailTask([guard, expectedName, contentType, previewKind, baseItem, imageBytes, richHtml, imageSize, sourceFilePath, mimeOffset, thumbnailDpr, itemScale]() mutable {
        PendingItemProcessingResult result;
        QByteArray resolvedImageBytes = imageBytes;
        QString resolvedHtml = richHtml;
        if ((resolvedImageBytes.isEmpty() || resolvedHtml.isEmpty())
            && !sourceFilePath.isEmpty()
            && (contentType == Image
                || contentType == Office
                || (contentType == RichText && previewKind == VisualPreview))) {
            QString htmlPayload;
            QByteArray imagePayload;
            LocalSaver::loadMimePayloads(sourceFilePath,
                                         mimeOffset,
                                         ((contentType == RichText && previewKind == VisualPreview)
                                             || contentType == Office) ? &htmlPayload : nullptr,
                                         (contentType == Image
                                            || contentType == Office
                                            || (contentType == RichText && previewKind == VisualPreview)) ? &imagePayload : nullptr);
            if (resolvedHtml.isEmpty()) {
                resolvedHtml = htmlPayload;
            }
            if (resolvedImageBytes.isEmpty()) {
                resolvedImageBytes = imagePayload;
            }
        }

        if ((contentType == Image || contentType == Office)
            && !resolvedImageBytes.isEmpty()) {
            result.thumbnailImage = ThumbnailBuilder::buildCardThumbnailImageFromBytes(resolvedImageBytes, thumbnailDpr, itemScale);
        } else if (contentType == Office
                   && !resolvedHtml.isEmpty()) {
            result.thumbnailImage = ThumbnailBuilder::buildRichTextThumbnailImageFromHtml(resolvedHtml, resolvedImageBytes, thumbnailDpr, itemScale);
        } else if (contentType == RichText
                   && previewKind == VisualPreview
                   && !resolvedHtml.isEmpty()) {
            result.thumbnailImage = ThumbnailBuilder::buildRichTextThumbnailImageFromHtml(resolvedHtml, resolvedImageBytes, thumbnailDpr, itemScale);
        } else if (contentType == Link && !baseItem.hasThumbnail()) {
            QString linkUrl;
            const QList<QUrl> urls = baseItem.getNormalizedUrls();
            if (!urls.isEmpty()) {
                const QUrl &first = urls.first();
                linkUrl = first.isLocalFile() ? first.toLocalFile() : first.toString();
            } else {
                linkUrl = baseItem.getNormalizedText().left(512).trimmed();
            }
            result.thumbnailImage = ThumbnailBuilder::buildLinkPreviewImage(linkUrl, baseItem.getTitle(), thumbnailDpr, itemScale);
        }

        // Save to disk in the worker thread.  Avoid QPixmap here — it
        // requires the GUI thread and would block the main event loop.
        QString savedFilePath;
        {
            if (guard) {
                savedFilePath = guard->filePathForItem(baseItem);
                LocalSaver saver;
                saver.saveToFile(baseItem, savedFilePath, result.thumbnailImage);
            }
        }

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, expectedName, baseItem, result, thumbnailDpr, savedFilePath]() mutable {
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
                    if (ThumbnailBuilder::isVeryTallImage(imageSize)) {
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

                // Update the service index from the saved file (lightweight)
                // and propagate sourceFilePath/mimeDataFileOffset so that
                // preview can read image data from disk after mimeData_ is
                // released.
                if (!savedFilePath.isEmpty()) {
                    LocalSaver indexSaver;
                    ClipboardItem lightItem = indexSaver.loadFromFileLight(savedFilePath);
                    if (!lightItem.getName().isEmpty()) {
                        const IndexedItemMeta meta = buildIndexedItemMeta(savedFilePath, lightItem);
                        const int existingIndex = guard->indexedFilePaths_.indexOf(savedFilePath);
                        if (existingIndex >= 0 && existingIndex < guard->indexedItems_.size()) {
                            guard->indexedItems_[existingIndex] = meta;
                        } else {
                            guard->indexedFilePaths_.prepend(savedFilePath);
                            guard->indexedItems_.prepend(meta);
                        }
                        preparedItem.setSourceFilePath(savedFilePath);
                        preparedItem.setMimeDataFileOffset(lightItem.mimeDataFileOffset());
                    }
                    guard->lastInternalWriteMs_ = QDateTime::currentMSecsSinceEpoch();
                    ++guard->internalWriteGen_;
                }

                emit guard->pendingItemReady(expectedName, preparedItem);
            }, Qt::QueuedConnection);
        }
    });
}

// --- Async thumbnail fetch ---

void ClipboardBoardService::requestThumbnailAsync(const QString &expectedName, const QString &filePath) {
    if (expectedName.isEmpty() || filePath.isEmpty()) {
        return;
    }

    QPointer<ClipboardBoardService> guard(this);
    startThumbnailTask([guard, expectedName, filePath]() mutable {
        LocalSaver saver;
        ClipboardItem loaded = saver.loadFromFileLight(filePath);
        ClipboardItem preparedItem = loaded;
        bool generatedThumbnail = false;
        bool refreshedRichText = false;
        bool attemptedRebuild = false;
        bool rebuildFailed = false;
        bool loadedPersistedThumbnail = false;
        const QString loadedNormalizedText = loaded.getNormalizedText();
        if (!loaded.getName().isEmpty()) {
            const ContentType type = loaded.getContentType();
            if (loaded.hasThumbnailHint() && loaded.thumbnail().isNull()) {
                ClipboardItem thumbnailItem = saver.loadFromFileLight(filePath, true);
                if (!thumbnailItem.getName().isEmpty() && !thumbnailItem.thumbnail().isNull()) {
                    preparedItem.setThumbnail(thumbnailItem.thumbnail());
                    loadedPersistedThumbnail = true;
                }
            }

            const bool shouldRebuild =
                (type == RichText && loaded.getPreviewKind() == VisualPreview)
                || (preparedItem.thumbnail().isNull()
                    && (type == Image
                        || type == Office));
            const bool alreadyFailed = [&]() {
                if (!guard) return false;
                QMutexLocker locker(&guard->failedFullLoadMutex_);
                return guard->failedFullLoadPaths_.contains(filePath);
            }();
            if (shouldRebuild && !alreadyFailed) {
                attemptedRebuild = true;
                if (type == RichText) {
                    QString htmlPayload;
                    QByteArray imagePayload;
                    if (LocalSaver::loadMimePayloads(filePath,
                                                     loaded.mimeDataFileOffset(),
                                                     &htmlPayload,
                                                     &imagePayload)
                        && !htmlPayload.isEmpty()) {
                        const qreal thumbnailDpr = ThumbnailBuilder::maxScreenDevicePixelRatio();
                        const int itemScale = MPasteSettings::getInst()->getItemScale();
                        const QImage thumbnailImage = ThumbnailBuilder::buildRichTextThumbnailImageFromHtml(htmlPayload,
                                                                                          imagePayload,
                                                                                          thumbnailDpr,
                                                                                          itemScale);
                        if (!thumbnailImage.isNull()) {
                            QPixmap thumbnail = QPixmap::fromImage(thumbnailImage);
                            thumbnail.setDevicePixelRatio(qMax<qreal>(1.0, thumbnailDpr));
                            preparedItem.setThumbnail(thumbnail);
                            generatedThumbnail = preparedItem.thumbnail().cacheKey() != loaded.thumbnail().cacheKey();
                            refreshedRichText = true;
                        }
                    }

                    if (!generatedThumbnail) {
                        ClipboardItem fullItem = saver.loadFromFile(filePath);
                        if (!fullItem.getName().isEmpty()) {
                            preparedItem = ThumbnailBuilder::prepareItemForDisplayAndSave(fullItem);
                            generatedThumbnail = !preparedItem.thumbnail().isNull()
                                && preparedItem.thumbnail().cacheKey() != loaded.thumbnail().cacheKey();
                            refreshedRichText = !preparedItem.thumbnail().isNull();
                        }
                    }

                    if (!generatedThumbnail && !refreshedRichText) {
                        rebuildFailed = true;
                    }
                } else if (type == Office) {
                    QString htmlPayload;
                    QByteArray imagePayload;
                    if (LocalSaver::loadMimePayloads(filePath,
                                                     loaded.mimeDataFileOffset(),
                                                     &htmlPayload,
                                                     &imagePayload)) {
                        const qreal thumbnailDpr = ThumbnailBuilder::maxScreenDevicePixelRatio();
                        const int itemScale = MPasteSettings::getInst()->getItemScale();
                        QImage thumbnailImage;
                        if (!imagePayload.isEmpty()) {
                            thumbnailImage = ThumbnailBuilder::buildCardThumbnailImageFromBytes(imagePayload,
                                                                                                thumbnailDpr,
                                                                                                itemScale);
                        }
                        if (thumbnailImage.isNull() && !htmlPayload.isEmpty()) {
                            thumbnailImage = ThumbnailBuilder::buildRichTextThumbnailImageFromHtml(htmlPayload,
                                                                                                   imagePayload,
                                                                                                   thumbnailDpr,
                                                                                                   itemScale);
                        }
                        if (!thumbnailImage.isNull()) {
                            QPixmap thumbnail = QPixmap::fromImage(thumbnailImage);
                            thumbnail.setDevicePixelRatio(qMax<qreal>(1.0, thumbnailDpr));
                            preparedItem.setThumbnail(thumbnail);
                            generatedThumbnail = preparedItem.thumbnail().cacheKey() != loaded.thumbnail().cacheKey();
                            refreshedRichText = !preparedItem.thumbnail().isNull();
                        }
                    }

                    if (!generatedThumbnail) {
                        ClipboardItem fullItem = saver.loadFromFile(filePath);
                        if (!fullItem.getName().isEmpty()) {
                            preparedItem = ThumbnailBuilder::prepareItemForDisplayAndSave(fullItem);
                            generatedThumbnail = !preparedItem.thumbnail().isNull()
                                && preparedItem.thumbnail().cacheKey() != loaded.thumbnail().cacheKey();
                            refreshedRichText = !preparedItem.thumbnail().isNull();
                        }
                    }

                    if (!generatedThumbnail && !refreshedRichText) {
                        rebuildFailed = true;
                    }
                } else {
                    ClipboardItem fullItem = saver.loadFromFile(filePath);
                    if (!fullItem.getName().isEmpty()) {
                        preparedItem = ThumbnailBuilder::prepareItemForDisplayAndSave(fullItem);
                        generatedThumbnail = !preparedItem.thumbnail().isNull()
                            && preparedItem.thumbnail().cacheKey() != loaded.thumbnail().cacheKey();
                        refreshedRichText = type == RichText;
                    } else {
                        rebuildFailed = true;
                    }
                }
            } else if (shouldRebuild) {
                rebuildFailed = true;
            }
        }
        const QPixmap thumbnail = preparedItem.thumbnail();
        const bool shouldPersistPreparedItem = generatedThumbnail
            || (refreshedRichText && preparedItem.getNormalizedText() != loadedNormalizedText);
        const bool noThumbnailProgress = attemptedRebuild
            && thumbnail.isNull()
            && !generatedThumbnail
            && !refreshedRichText
            && !loadedPersistedThumbnail;

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, expectedName, filePath, preparedItem, thumbnail, generatedThumbnail, refreshedRichText, shouldPersistPreparedItem, rebuildFailed, noThumbnailProgress]() {
                if (!guard) {
                    return;
                }
                if (rebuildFailed && !filePath.isEmpty()) {
                    QMutexLocker locker(&guard->failedFullLoadMutex_);
                    guard->failedFullLoadPaths_.insert(filePath);
                }
                if ((generatedThumbnail || refreshedRichText) && !preparedItem.getName().isEmpty()) {
                    // Emit the thumbnail immediately so the UI updates
                    // without any disk I/O on the main thread.  Persist
                    // the file later via a deferred call so the current
                    // event-loop iteration stays responsive.
                    emit guard->thumbnailReady(expectedName, thumbnail);
                    if (shouldPersistPreparedItem) {
                        guard->saveItemQuiet(preparedItem);
                    }
                    return;
                }
                if (rebuildFailed || noThumbnailProgress) {
                    emit guard->thumbnailReady(expectedName, QPixmap());
                    return;
                }
                emit guard->thumbnailReady(expectedName, thumbnail);
            }, Qt::QueuedConnection);
        }
    });
}

// --- Async keyword search ---

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
