// input: Depends on ClipboardBoardService.h, LocalSaver, MPasteSettings, and Qt IO/threading utilities.
// output: Implements board persistence, deferred loading, thumbnail processing, and keyword search routines.
// pos: utils layer board service implementation.
// update: If I change, update this header block and my folder README.md.
// note: Thumbnail decode now accepts Qt serialized image payloads, uses shared card preview metrics, respects data-layer preview kind for rich text, trims rich-text margins, backfills missing on-disk thumbnails on demand, and uses bounded worker concurrency.
#include "ClipboardBoardService.h"

#include <algorithm>
#include <numeric>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QRunnable>

#include "data/LocalSaver.h"
#include "utils/MPasteSettings.h"
#include "utils/ThumbnailBuilder.h"

namespace {

ClipboardBoardService::IndexedItemMeta buildIndexedItemMeta(const QString &filePath,
                                                            const ClipboardItem &item) {
    ClipboardBoardService::IndexedItemMeta meta;
    meta.filePath = filePath;
    meta.name = item.getName();
    meta.title = item.getTitle();
    meta.url = item.getUrl();
    meta.alias = item.getAlias();
    meta.normalizedUrls = item.getNormalizedUrls();
    meta.fingerprint = item.fingerprint();
    meta.time = item.getTime();
    meta.contentType = item.getContentType();
    meta.previewKind = item.getPreviewKind();
    meta.mimeDataOffset = item.mimeDataFileOffset();
    meta.pinned = item.isPinned();
    meta.hasThumbnailHint = item.hasThumbnailHint();

    QStringList searchParts;
    if (!meta.alias.isEmpty()) {
        searchParts << meta.alias;
    }
    if (!meta.title.isEmpty()) {
        searchParts << meta.title;
    }
    if (!meta.url.isEmpty()) {
        searchParts << meta.url;
    }
    const QString normalizedText = item.getNormalizedText();
    if (!normalizedText.isEmpty()) {
        // Keep only enough text for typical keyword matching in the index;
        // full-text search falls back to async file-based matching.
        searchParts << normalizedText.left(512);
    }
    for (const QUrl &url : meta.normalizedUrls) {
        searchParts << (url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded));
    }
    meta.searchableText = searchParts.join(QLatin1Char('\n')).toLower();
    return meta;
}

bool indexedItemMatchesFilter(const ClipboardBoardService::IndexedItemMeta &item,
                              ContentType type,
                              const QString &keyword,
                              const QSet<QString> &matchedNames) {
    if (item.name.isEmpty()) {
        return false;
    }
    if (type != All && item.contentType != type) {
        return false;
    }
    if (keyword.isEmpty()) {
        return true;
    }
    return item.searchableText.contains(keyword, Qt::CaseInsensitive) || matchedNames.contains(item.name);
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

struct PendingItemProcessingResult {
    QImage thumbnailImage;
};

} // namespace

ClipboardBoardService::ClipboardBoardService(const QString &category, QObject *parent)
    : QObject(parent),
      category_(category),
      saver_(std::make_unique<LocalSaver>()),
      thumbnailTaskPool_(std::make_unique<QThreadPool>()) {
    deferredLoadTimer_ = new QTimer(this);
    deferredLoadTimer_->setSingleShot(true);
    connect(deferredLoadTimer_, &QTimer::timeout, this, &ClipboardBoardService::continueDeferredLoad);

    const int idealThreadCount = qMax(1, QThread::idealThreadCount());
    thumbnailTaskPool_->setMaxThreadCount(qBound(1, idealThreadCount / 2, 4));
    thumbnailTaskPool_->setExpiryTimeout(15000);

    deferredSaveTimer_ = new QTimer(this);
    deferredSaveTimer_->setSingleShot(true);
    deferredSaveTimer_->setInterval(500);
    connect(deferredSaveTimer_, &QTimer::timeout, this, [this]() {
        const QList<ClipboardItem> batch = std::move(pendingSaveQueue_);
        pendingSaveQueue_.clear();
        for (const ClipboardItem &item : batch) {
            saveItemQuiet(item);
        }
    });
}

ClipboardBoardService::~ClipboardBoardService() {
    // Flush any pending deferred saves before destruction.
    if (deferredSaveTimer_) {
        deferredSaveTimer_->stop();
    }
    for (const ClipboardItem &item : pendingSaveQueue_) {
        saveItemQuiet(item);
    }
    pendingSaveQueue_.clear();

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
    if (thumbnailTaskPool_) {
        thumbnailTaskPool_->waitForDone();
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

    Q_UNUSED(filePaths);
    Q_UNUSED(initialBatchSize);
    Q_UNUSED(deferredBatchSize);

    emit deferredLoadCompleted();
}

void ClipboardBoardService::refreshIndex() {
    deferredLoadActive_ = false;
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    waitForDeferredRead();
    waitForIndexRefresh();
    deferredLoadedItems_.clear();
    indexedItems_.clear();
    indexedFilePaths_.clear();
    pendingLoadFilePaths_.clear();
    failedFullLoadPaths_.clear();
    updateTotalItemCount(0);
    checkSaveDir();
    emit pendingCountChanged(0);
}

ClipboardBoardService::IncrementalSyncResult ClipboardBoardService::syncIndexIncremental() {
    IncrementalSyncResult result;
    checkSaveDir();
    QDir dir(saveDir());
    const QFileInfoList fileInfos = dir.entryInfoList(
        QStringList() << QStringLiteral("*.mpaste"), QDir::Files);

    // Build the set of all .mpaste file paths on disk using only the
    // directory listing — do NOT open every file to check its format,
    // as that is extremely expensive for large histories (O(N) file I/O
    // on the main thread).
    QSet<QString> diskPaths;
    diskPaths.reserve(fileInfos.size());
    for (const QFileInfo &info : fileInfos) {
        diskPaths.insert(info.filePath());
    }

    // Find removed: in index but not on disk.
    const QSet<QString> indexedSet(indexedFilePaths_.begin(), indexedFilePaths_.end());
    for (const QString &path : indexedSet) {
        if (!diskPaths.contains(path)) {
            result.removedPaths.append(path);
        }
    }

    // Find added: on disk but not in index.  Only validate the format
    // of genuinely new files (typically just a handful per wake).
    for (const QString &path : diskPaths) {
        if (!indexedSet.contains(path)) {
            if (LocalSaver::isCurrentFormatFile(path)) {
                result.addedPaths.append(path);
            }
        }
    }

    // Apply removals to the index.
    for (const QString &path : result.removedPaths) {
        const int idx = indexedFilePaths_.indexOf(path);
        if (idx >= 0) {
            indexedFilePaths_.removeAt(idx);
            if (idx < indexedItems_.size()) {
                indexedItems_.removeAt(idx);
            }
        }
    }

    // Apply additions to the index.
    for (const QString &path : result.addedPaths) {
        ClipboardItem lightItem = saver_->loadFromFileLight(path);
        if (lightItem.getName().isEmpty()) {
            continue;
        }
        const IndexedItemMeta meta = buildIndexedItemMeta(path, lightItem);
        indexedFilePaths_.prepend(path);
        indexedItems_.prepend(meta);
    }

    if (!result.addedPaths.isEmpty() || !result.removedPaths.isEmpty()) {
        updateTotalItemCount(indexedItems_.size());
    }
    return result;
}

void ClipboardBoardService::startAsyncLoad(int initialBatchSize, int deferredBatchSize) {
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    deferredLoadActive_ = false;
    waitForDeferredRead();
    deferredLoadedItems_.clear();
    indexedItems_.clear();
    indexedFilePaths_.clear();
    pendingLoadFilePaths_.clear();
    failedFullLoadPaths_.clear();
    updateTotalItemCount(0);
    emit pendingCountChanged(0);
    checkSaveDir();

    const quint64 token = ++asyncLoadToken_;
    const QString directory = saveDir();
    QPointer<ClipboardBoardService> guard(this);
    QThread *thread = startTrackedThread([guard, directory, initialBatchSize, deferredBatchSize, token]() {
        QList<ClipboardBoardService::IndexedItemMeta> indexedItems;
        QStringList filePaths;
        LocalSaver saver;
        QDir dir(directory);
        const QFileInfoList fileInfos = dir.entryInfoList(QStringList() << "*.mpaste", QDir::Files);
        indexedItems.reserve(fileInfos.size());
        filePaths.reserve(fileInfos.size());
        for (const QFileInfo &info : fileInfos) {
            if (LocalSaver::isCurrentFormatFile(info.filePath())) {
                const QString filePath = info.filePath();
                ClipboardItem item = saver.loadFromFileLight(filePath);
                if (item.getName().isEmpty()) {
                    qWarning().noquote() << QStringLiteral("[board-service] skip unreadable history file during index build path=%1")
                        .arg(filePath);
                    continue;
                }
                filePaths.append(filePath);
                indexedItems.append(buildIndexedItemMeta(filePath, item));
            }
        }
        // Sort by header time (newest first) instead of relying on
        // filename order, so items display correctly even if the
        // filename timestamp diverges from the stored time.
        QList<int> order(indexedItems.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return indexedItems[a].time > indexedItems[b].time;
        });
        {
            QList<ClipboardBoardService::IndexedItemMeta> sortedItems;
            QStringList sortedPaths;
            sortedItems.reserve(indexedItems.size());
            sortedPaths.reserve(filePaths.size());
            for (int idx : order) {
                sortedItems.append(indexedItems[idx]);
                sortedPaths.append(filePaths[idx]);
            }
            indexedItems = std::move(sortedItems);
            filePaths = std::move(sortedPaths);
        }

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, indexedItems, filePaths, initialBatchSize, deferredBatchSize, token]() {
                if (guard) {
                    if (token != guard->asyncLoadToken_) {
                        return;
                    }
                    // Preserve items added via saveItemQuiet() during the
                    // async scan window — the scan started before those
                    // files were written, so they are missing from the
                    // scan result.
                    QList<IndexedItemMeta> locallyAdded;
                    QStringList locallyAddedPaths;
                    for (int i = 0; i < guard->indexedItems_.size(); ++i) {
                        const QString &path = (i < guard->indexedFilePaths_.size())
                            ? guard->indexedFilePaths_.at(i)
                            : guard->indexedItems_.at(i).filePath;
                        if (!filePaths.contains(path)) {
                            locallyAdded.append(guard->indexedItems_.at(i));
                            locallyAddedPaths.append(path);
                        }
                    }

                    guard->indexedItems_ = indexedItems;
                    guard->indexedFilePaths_ = filePaths;
                    guard->pendingLoadFilePaths_ = filePaths;

                    // Re-insert locally added items at the front.
                    for (int i = locallyAdded.size() - 1; i >= 0; --i) {
                        guard->indexedItems_.prepend(locallyAdded.at(i));
                        guard->indexedFilePaths_.prepend(locallyAddedPaths.at(i));
                    }
                    guard->updateTotalItemCount(guard->indexedItems_.size());
                    emit guard->pendingCountChanged(guard->pendingLoadFilePaths_.size());
                    if (guard->pendingLoadFilePaths_.isEmpty()) {
                        guard->deferredLoadActive_ = false;
                        emit guard->deferredLoadCompleted();
                        return;
                    }
                    guard->deferredLoadActive_ = deferredBatchSize > 0;
                    guard->deferredBatchSize_ = qMax(1, deferredBatchSize);
                    guard->loadNextBatch(initialBatchSize > 0 ? initialBatchSize
                                                              : (guard->deferredBatchSize_ > 0 ? guard->deferredBatchSize_ : 1));
                    if (guard->deferredLoadActive_ && !guard->pendingLoadFilePaths_.isEmpty() && guard->deferredLoadTimer_) {
                        guard->deferredLoadTimer_->start(guard->visibleHint_ ? 0 : 8);
                    } else if (guard->pendingLoadFilePaths_.isEmpty()) {
                        guard->deferredLoadActive_ = false;
                        emit guard->deferredLoadCompleted();
                    }
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
        ClipboardItem item = saver_->loadFromFileLight(filePath, true);
        if (item.getName().isEmpty()) {
            qWarning().noquote() << QStringLiteral("[board-service] skip unreadable history file during loadNextBatch path=%1")
                .arg(filePath);
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
    return ThumbnailBuilder::prepareItemForDisplayAndSave(source);
}

void ClipboardBoardService::saveItem(const ClipboardItem &item) {
    const bool isNew = saveItemInternal(item);
    if (isNew) {
        updateTotalItemCount(indexedItems_.size());
    }
    emit localPersistenceChanged();
}

void ClipboardBoardService::saveItemQuiet(const ClipboardItem &item) {
    saveItemInternal(item);
    // No signals — caller is responsible for keeping UI in sync.
}

void ClipboardBoardService::scheduleDeferredSave(const ClipboardItem &item) {
    // Replace any pending save for the same item, keep only the latest.
    for (int i = 0; i < pendingSaveQueue_.size(); ++i) {
        if (pendingSaveQueue_[i].getName() == item.getName()) {
            pendingSaveQueue_[i] = item;
            deferredSaveTimer_->start();
            return;
        }
    }
    pendingSaveQueue_.append(item);
    deferredSaveTimer_->start();
}

bool ClipboardBoardService::hasRecentInternalWrite() const {
    return (QDateTime::currentMSecsSinceEpoch() - lastInternalWriteMs_) < 2000;
}

quint64 ClipboardBoardService::internalWriteGeneration() const {
    return internalWriteGen_;
}

bool ClipboardBoardService::saveItemInternal(const ClipboardItem &item) {
    lastInternalWriteMs_ = QDateTime::currentMSecsSinceEpoch();
    ++internalWriteGen_;
    checkSaveDir();
    const QString filePath = filePathForItem(item);
    const bool knownPath = indexedFilePaths_.contains(filePath);
    saver_->saveToFile(item, filePath);
    ClipboardItem lightItem = saver_->loadFromFileLight(filePath);
    if (!lightItem.getName().isEmpty()) {
        const IndexedItemMeta meta = buildIndexedItemMeta(filePath, lightItem);
        const int existingIndex = indexedFilePaths_.indexOf(filePath);
        if (existingIndex >= 0 && existingIndex < indexedItems_.size()) {
            indexedItems_[existingIndex] = meta;
        } else if (!filePath.isEmpty()) {
            indexedFilePaths_.prepend(filePath);
            indexedItems_.prepend(meta);
        }
    }
    return !knownPath && !filePath.isEmpty();
}

void ClipboardBoardService::removeItemFile(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }
    saver_->removeItem(filePath);
    const int index = indexedFilePaths_.indexOf(filePath);
    if (index >= 0) {
        indexedFilePaths_.removeAt(index);
        if (index < indexedItems_.size()) {
            indexedItems_.removeAt(index);
        }
    }
    pendingLoadFilePaths_.removeAll(filePath);
    emit localPersistenceChanged();
}

void ClipboardBoardService::deleteItemByPath(const QString &filePath) {
    deleteItemByPathInternal(filePath);
    emit localPersistenceChanged();
}

void ClipboardBoardService::deleteItemByPathQuiet(const QString &filePath) {
    deleteItemByPathInternal(filePath);
}

void ClipboardBoardService::deleteItemByPathInternal(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    lastInternalWriteMs_ = QDateTime::currentMSecsSinceEpoch();
    ++internalWriteGen_;
    saver_->removeItem(filePath);
    const int index = indexedFilePaths_.indexOf(filePath);
    if (index >= 0) {
        indexedFilePaths_.removeAt(index);
        if (index < indexedItems_.size()) {
            indexedItems_.removeAt(index);
        }
    }
    const int pendingIndex = pendingLoadFilePaths_.indexOf(filePath);
    if (pendingIndex >= 0) {
        pendingLoadFilePaths_.removeAt(pendingIndex);
    }
    totalItemCount_ = qMax(0, totalItemCount_ - 1);
}

bool ClipboardBoardService::deletePendingItemByPath(const QString &filePath) {
    if (filePath.isEmpty()) {
        return false;
    }

    const int pendingIndex = pendingLoadFilePaths_.indexOf(filePath);
    if (pendingIndex < 0) {
        return false;
    }

    const int index = indexedFilePaths_.indexOf(filePath);
    if (index >= 0) {
        indexedFilePaths_.removeAt(index);
        if (index < indexedItems_.size()) {
            indexedItems_.removeAt(index);
        }
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

ClipboardItem ClipboardBoardService::loadItemLight(const QString &filePath, bool includeThumbnail) {
    return saver_->loadFromFileLight(filePath, includeThumbnail);
}

void ClipboardBoardService::refreshIndexedItemForPath(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    const int existingIndex = indexedFilePaths_.indexOf(filePath);
    if (!QFileInfo::exists(filePath) || !LocalSaver::isCurrentFormatFile(filePath)) {
        if (existingIndex >= 0) {
            indexedFilePaths_.removeAt(existingIndex);
            if (existingIndex < indexedItems_.size()) {
                indexedItems_.removeAt(existingIndex);
            }
            updateTotalItemCount(indexedItems_.size());
        }
        emit localPersistenceChanged();
        return;
    }

    ClipboardItem lightItem = saver_->loadFromFileLight(filePath);
    if (lightItem.getName().isEmpty()) {
        if (existingIndex >= 0) {
            indexedFilePaths_.removeAt(existingIndex);
            if (existingIndex < indexedItems_.size()) {
                indexedItems_.removeAt(existingIndex);
            }
            updateTotalItemCount(indexedItems_.size());
        }
        emit localPersistenceChanged();
        return;
    }

    const IndexedItemMeta meta = buildIndexedItemMeta(filePath, lightItem);
    if (existingIndex >= 0 && existingIndex < indexedItems_.size()) {
        indexedItems_[existingIndex] = meta;
    } else {
        indexedFilePaths_.prepend(filePath);
        indexedItems_.prepend(meta);
        updateTotalItemCount(indexedItems_.size());
    }
    emit localPersistenceChanged();
}

int ClipboardBoardService::filteredItemCount(ContentType type,
                                             const QString &keyword,
                                             const QSet<QString> &matchedNames) const {
    if (indexedItems_.isEmpty()) {
        return 0;
    }

    int count = 0;
    for (const auto &entry : indexedItems_) {
        if (indexedItemMatchesFilter(entry, type, keyword, matchedNames)) {
            ++count;
        }
    }
    return count;
}

QList<QPair<QString, ClipboardItem>> ClipboardBoardService::loadIndexedSlice(int offset, int count, bool includeThumbnail) {
    QList<QPair<QString, ClipboardItem>> loadedItems;
    if (count <= 0 || offset < 0 || offset >= indexedItems_.size()) {
        return loadedItems;
    }

    const int end = qMin(indexedItems_.size(), offset + count);
    loadedItems.reserve(end - offset);
    for (int i = offset; i < end; ++i) {
        const QString &filePath = indexedItems_.at(i).filePath;
        ClipboardItem item = saver_->loadFromFileLight(filePath, includeThumbnail);
        if (item.getName().isEmpty()) {
            continue;
        }
        loadedItems.append(qMakePair(filePath, item));
    }
    return loadedItems;
}

QList<QPair<QString, ClipboardItem>> ClipboardBoardService::loadFilteredIndexedSlice(ContentType type,
                                                                                      const QString &keyword,
                                                                                      const QSet<QString> &matchedNames,
                                                                                      int offset,
                                                                                      int count,
                                                                                      bool includeThumbnail) {
    QList<QPair<QString, ClipboardItem>> loadedItems;
    if (count <= 0 || offset < 0 || indexedItems_.isEmpty()) {
        return loadedItems;
    }

    int matchedIndex = 0;
    for (int i = 0; i < indexedItems_.size(); ++i) {
        const auto &entry = indexedItems_.at(i);
        if (!indexedItemMatchesFilter(entry, type, keyword, matchedNames)) {
            continue;
        }
        if (matchedIndex++ < offset) {
            continue;
        }
        const QString &filePath = entry.filePath;
        ClipboardItem item = saver_->loadFromFileLight(filePath, includeThumbnail);
        if (item.getName().isEmpty()) {
            continue;
        }
        loadedItems.append(qMakePair(filePath, item));
        if (loadedItems.size() >= count) {
            break;
        }
    }
    return loadedItems;
}

QSet<QByteArray> ClipboardBoardService::loadAllFingerprints() {
    QSet<QByteArray> fingerprints;
    if (!indexedItems_.isEmpty()) {
        for (const auto &entry : indexedItems_) {
            if (!entry.name.isEmpty()) {
                fingerprints.insert(entry.fingerprint);
            }
        }
        return fingerprints;
    }

    checkSaveDir();
    QDir dir(saveDir());
    const QFileInfoList fileInfos = dir.entryInfoList(QStringList() << "*.mpaste", QDir::Files);
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

bool ClipboardBoardService::containsFingerprint(const QByteArray &fingerprint) const {
    if (fingerprint.isEmpty()) {
        return false;
    }
    for (const auto &entry : indexedItems_) {
        if (entry.fingerprint == fingerprint) {
            return true;
        }
    }
    return false;
}

void ClipboardBoardService::notifyItemAdded() {
    updateTotalItemCount(totalItemCount_ + 1);
}

bool ClipboardBoardService::moveIndexedItemToFront(const QString &name) {
    const QString filePath = filePathForName(name);
    if (filePath.isEmpty()) {
        return false;
    }

    const int index = indexedFilePaths_.indexOf(filePath);
    if (index <= 0) {
        return index == 0;
    }

    indexedFilePaths_.move(index, 0);
    if (index < indexedItems_.size()) {
        indexedItems_.move(index, 0);
    }
    return true;
}

void ClipboardBoardService::updateIndexedItemTime(const QString &name, const QDateTime &time) {
    const QString filePath = filePathForName(name);
    const int idx = indexedFilePaths_.indexOf(filePath);
    if (idx >= 0 && idx < indexedItems_.size()) {
        indexedItems_[idx].time = time;
    }
}

void ClipboardBoardService::trimExpiredPendingItems(const QDateTime &cutoff) {
    Q_UNUSED(cutoff);
    if (category_ == MPasteSettings::STAR_CATEGORY_NAME) {
        return;
    }
}

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
            if (shouldRebuild && !(guard && guard->failedFullLoadPaths_.contains(filePath))) {
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

    if (!pendingLoadFilePaths_.isEmpty()) {
        loadNextBatch(deferredBatchSize_);
    }

    if (pendingLoadFilePaths_.isEmpty()) {
        deferredLoadActive_ = false;
        emit deferredLoadCompleted();
    } else if (deferredLoadTimer_) {
        deferredLoadTimer_->start(visibleHint_ ? 0 : 8);
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
            qWarning().noquote() << QStringLiteral("[board-service] preserve unreadable deferred history file path=%1 rawSize=%2")
                .arg(filePath)
                .arg(rawData.size());
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
