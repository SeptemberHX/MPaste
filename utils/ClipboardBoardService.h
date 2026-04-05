// input: Depends on ClipboardItem data, MPaste settings, and Qt threading/timer APIs.
// output: Provides board-level persistence, deferred loading, and background processing services.
// pos: utils layer board service.
// update: If I change, update this header block and my folder README.md.
// note: Adds async thumbnail fetch for on-demand UI loading with bounded worker concurrency.
//
// Implementation split
// --------------------
// The implementation is spread across 3 .cpp files:
//   - ClipboardBoardService.cpp    (core) — constructor, index management,
//     loading/scheduling, query methods, deferred load internals.
//   - ClipboardBoardServiceIO.cpp  (persistence) — save, delete, file
//     paths, deferred save.  All disk write operations live here.
//   - ClipboardBoardServiceAsync.cpp (background) — thumbnail processing,
//     thumbnail fetch, keyword search, thread management.  All worker-
//     thread creation lives here.
//
// When adding new functionality:
//   - Disk I/O → IO file.
//   - Background/async work → Async file.
//   - Index queries or load scheduling → core file.
//
// Threading model
// ---------------
// Main-thread-only methods (called from the GUI/event-loop thread):
//   - All public API methods except where noted below.
//   - All slots (continueDeferredLoad, handleDeferredBatchRead).
//   - All private helpers that mutate indexedItems_, indexedFilePaths_,
//     pendingLoadFilePaths_, deferredLoadedItems_, and totalItemCount_.
//
// Worker-thread lambdas (run on QThread or QThreadPool):
//   - startAsyncLoad:            Builds a LOCAL IndexedItemMeta list from disk;
//                                does NOT read indexedItems_ or indexedFilePaths_.
//   - startAsyncKeywordSearch:   Operates on a copied candidate list; does NOT
//                                read service fields.
//   - startRawReadBatch:         Reads files from a copied path list; does NOT
//                                read service fields.
//   - processPendingItemAsync:   Captures item data by value before dispatch.
//                                Reads failedFullLoadPaths_ (race-benign hint)
//                                and calls filePathForItem (reads const category_).
//   - requestThumbnailAsync:     Same pattern as processPendingItemAsync.
//
// Fields accessed from worker threads:
//   - category_ (immutable after construction -- safe).
//   - failedFullLoadPaths_ (read from workers, written on main thread via
//     invokeMethod callback -- guarded by failedFullLoadMutex_).
//
// Cross-thread communication:
//   All worker lambdas capture a QPointer<ClipboardBoardService> guard and
//   deliver results back to the main thread via QMetaObject::invokeMethod
//   with Qt::QueuedConnection.  The guard is checked both before posting and
//   inside the queued callback to handle service destruction.
//
// indexLock_ (QReadWriteLock):
//   Provided for future use if worker threads ever need direct read access to
//   indexedItems_ / indexedFilePaths_.  Currently no worker reads these fields
//   so no lock acquisitions are present -- all mutations and reads happen on
//   the main thread.
#ifndef MPASTE_CLIPBOARD_BOARD_SERVICE_H
#define MPASTE_CLIPBOARD_BOARD_SERVICE_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QObject>
#include <QPixmap>
#include <QMutex>
#include <QReadWriteLock>

#include <memory>
#include <functional>

#include "data/ClipboardItem.h"

class LocalSaver;
class QThread;
class QTimer;
class QThreadPool;

class ClipboardBoardService : public QObject {
    Q_OBJECT

public:
    struct IndexedItemMeta {
        QString filePath;
        QString name;
        QString title;
        QString url;
        QString alias;
        QString searchableText;
        QList<QUrl> normalizedUrls;
        QByteArray fingerprint;
        QDateTime time;
        ContentType contentType = Text;
        ClipboardPreviewKind previewKind = TextPreview;
        quint64 mimeDataOffset = 0;
        bool pinned = false;
        bool hasThumbnailHint = false;
    };

    explicit ClipboardBoardService(const QString &category, QObject *parent = nullptr);
    ~ClipboardBoardService() override;

    QString category() const;
    QString saveDir() const;

    int totalItemCount() const;
    int pendingCount() const;
    bool hasPendingItems() const;
    bool deferredLoadActive() const;
    bool hasRecentInternalWrite() const;
    quint64 internalWriteGeneration() const;

    void refreshIndex();
    struct IncrementalSyncResult {
        QStringList addedPaths;
        QStringList removedPaths;
    };
    IncrementalSyncResult syncIndexIncremental();
    void startAsyncLoad(int initialBatchSize, int deferredBatchSize);
    void loadNextBatch(int batchSize);
    void ensureAllItemsLoaded(int batchSize);

    void startDeferredLoad(int batchSize);
    void stopDeferredLoad();
    void setVisibleHint(bool visible);

    ClipboardItem prepareItemForSave(const ClipboardItem &source) const;
    void saveItem(const ClipboardItem &item);
    void saveItemQuiet(const ClipboardItem &item);
    void scheduleDeferredSave(const ClipboardItem &item);
    void removeItemFile(const QString &filePath);
    void deleteItemByPath(const QString &filePath);
    void deleteItemByPathQuiet(const QString &filePath);
    bool deletePendingItemByPath(const QString &filePath);
    QString filePathForItem(const ClipboardItem &item) const;
    QString filePathForName(const QString &name) const;
    ClipboardItem loadItemLight(const QString &filePath, bool includeThumbnail = false);
    void refreshIndexedItemForPath(const QString &filePath);
    int filteredItemCount(ContentType type,
                          const QString &keyword,
                          const QSet<QString> &matchedNames) const;
    QList<QPair<QString, ClipboardItem>> loadIndexedSlice(int offset, int count, bool includeThumbnail = false);
    QList<QPair<QString, ClipboardItem>> loadFilteredIndexedSlice(ContentType type,
                                                                  const QString &keyword,
                                                                  const QSet<QString> &matchedNames,
                                                                  int offset,
                                                                  int count,
                                                                  bool includeThumbnail = false);
    QSet<QByteArray> loadAllFingerprints();
    bool containsFingerprint(const QByteArray &fingerprint) const;
    const QList<IndexedItemMeta> &indexedItemsMeta() const { return indexedItems_; }
    void notifyItemAdded();
    bool moveIndexedItemToFront(const QString &name);
    void updateIndexedItemTime(const QString &name, const QDateTime &time);
    QStringList trimExpiredItems(const QDateTime &cutoff);
    void processPendingItemAsync(const ClipboardItem &item, const QString &expectedName);
    void requestThumbnailAsync(const QString &expectedName, const QString &filePath);
    void startAsyncKeywordSearch(const QList<QPair<QString, quint64>> &candidates,
                                 const QString &keyword,
                                 quint64 token);

signals:
    void itemsLoaded(const QList<QPair<QString, ClipboardItem>> &items);
    void pendingItemReady(const QString &expectedName, const ClipboardItem &item);
    void thumbnailReady(const QString &expectedName, const QPixmap &thumbnail);
    void keywordMatched(const QSet<QString> &matchedNames, quint64 token);
    void localPersistenceChanged();
    void totalItemCountChanged(int total);
    void pendingCountChanged(int pending);
    void deferredLoadCompleted();

private slots:
    void continueDeferredLoad();
    void handleDeferredBatchRead(const QList<QPair<QString, QByteArray>> &batchPayloads);

private:
    QThread *startTrackedThread(const std::function<void()> &task);
    void startThumbnailTask(const std::function<void()> &task);
    void trackExclusiveThread(QThread *thread, QThread **slot);
    void startRawReadBatch(int batchSize);
    void applyPendingFileIndex(const QStringList &filePaths, int initialBatchSize, int deferredBatchSize, quint64 token);
    bool saveItemInternal(const ClipboardItem &item);
    void deleteItemByPathInternal(const QString &filePath);
    void checkSaveDir();
    void updateTotalItemCount(int total);
    void decrementTotalItemCount();
    void scheduleDeferredLoadBatch();
    void processDeferredLoadedItems();
    void waitForDeferredRead();
    void waitForIndexRefresh();

    QString category_;
    std::unique_ptr<LocalSaver> saver_;
    QTimer *deferredLoadTimer_ = nullptr;
    QThread *indexRefreshThread_ = nullptr;
    QThread *deferredLoadThread_ = nullptr;
    QThread *keywordSearchThread_ = nullptr;
    QList<QThread *> processingThreads_;
    std::unique_ptr<QThreadPool> thumbnailTaskPool_;
    QSet<QString> failedFullLoadPaths_;
    mutable QMutex failedFullLoadMutex_;
    mutable QReadWriteLock indexLock_;
    QList<IndexedItemMeta> indexedItems_;
    QStringList indexedFilePaths_;
    QStringList pendingLoadFilePaths_;
    QList<QPair<QString, QByteArray>> deferredLoadedItems_;
    int totalItemCount_ = 0;
    int deferredBatchSize_ = 0;
    quint64 asyncLoadToken_ = 0;
    bool deferredLoadActive_ = false;
    bool visibleHint_ = false;
    qint64 lastInternalWriteMs_ = 0;
    quint64 internalWriteGen_ = 0;
    QList<ClipboardItem> pendingSaveQueue_;
    QTimer *deferredSaveTimer_ = nullptr;
};

#endif // MPASTE_CLIPBOARD_BOARD_SERVICE_H
