// input: Depends on ClipboardItem data, MPaste settings, and Qt threading/timer APIs.
// output: Provides board-level persistence, deferred loading, and background processing services.
// pos: utils layer board service.
// update: If I change, update this header block and my folder README.md.
// note: Adds async thumbnail fetch for on-demand UI loading.
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

#include <memory>

#include "data/ClipboardItem.h"

class LocalSaver;
class QThread;
class QTimer;

class ClipboardBoardService : public QObject {
    Q_OBJECT

public:
    explicit ClipboardBoardService(const QString &category, QObject *parent = nullptr);
    ~ClipboardBoardService() override;

    QString category() const;
    QString saveDir() const;

    int totalItemCount() const;
    int pendingCount() const;
    bool hasPendingItems() const;
    bool deferredLoadActive() const;

    void refreshIndex();
    void loadNextBatch(int batchSize);
    void ensureAllItemsLoaded(int batchSize);

    void startDeferredLoad(int batchSize);
    void stopDeferredLoad();
    void setVisibleHint(bool visible);

    ClipboardItem prepareItemForSave(const ClipboardItem &source) const;
    void saveItem(const ClipboardItem &item);
    void removeItemFile(const QString &filePath);
    void deleteItemByPath(const QString &filePath);
    bool deletePendingItemByPath(const QString &filePath);
    QString filePathForItem(const ClipboardItem &item) const;
    QString filePathForName(const QString &name) const;
    ClipboardItem loadItemLight(const QString &filePath);
    void notifyItemAdded();
    void trimExpiredPendingItems(const QDateTime &cutoff);

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
    void checkSaveDir();
    void updateTotalItemCount(int total);
    void decrementTotalItemCount();
    void scheduleDeferredLoadBatch();
    void processDeferredLoadedItems();
    void waitForDeferredRead();

    QString category_;
    std::unique_ptr<LocalSaver> saver_;
    QTimer *deferredLoadTimer_ = nullptr;
    QThread *deferredLoadThread_ = nullptr;
    QThread *keywordSearchThread_ = nullptr;
    QList<QThread *> processingThreads_;
    QStringList pendingLoadFilePaths_;
    QList<QPair<QString, QByteArray>> deferredLoadedItems_;
    int totalItemCount_ = 0;
    int deferredBatchSize_ = 0;
    bool deferredLoadActive_ = false;
    bool visibleHint_ = false;
};

#endif // MPASTE_CLIPBOARD_BOARD_SERVICE_H
