// input: Depends on Qt Widgets, ClipboardItem data, persistence service, and item widgets.
// output: Exposes one history board with lazy loading, filtering, dedup indexing, and paste signals.
// pos: Widget-layer horizontal board declaration for clipboard/favorites views.
// update: If I change, update this header block and my folder README.md.
#ifndef SCROLLITEMSWIDGET_H
#define SCROLLITEMSWIDGET_H

#include <QWidget>
#include <QByteArray>
#include <QHBoxLayout>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <data/LocalSaver.h>
#include "data/ClipboardItem.h"
#include "ClipboardItemWidget.h"

class QPropertyAnimation;
class QResizeEvent;
class QTimer;
class QThread;
class QWheelEvent;
class QShowEvent;

namespace Ui {
class ScrollItemsWidget;
}

class ScrollItemsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScrollItemsWidget(const QString &category, const QString &borderColor, QWidget *parent = nullptr);
    ~ScrollItemsWidget();

    bool addOneItem(const ClipboardItem &item);
    bool addAndSaveItem(const ClipboardItem &item);
    void filterByKeyword(const QString &keyword);
    void filterByType(ClipboardItem::ContentType type);
    void setShortcutInfo();
    void cleanShortCutInfo();
    void loadFromSaveDir();
    void loadFromSaveDirDeferred();
    QScrollBar* horizontalScrollbar();
    void setAllItemVisible();
    const ClipboardItem* currentSelectedItem() const;
    const ClipboardItem* selectedByShortcut(int visibleOrder);
    const ClipboardItem* selectedByEnter();
    void focusMoveLeft();
    void focusMoveRight();
    int getItemCount();

    void scrollToFirst();
    void scrollToLast();
    QString getCategory() const;
    void removeItemByContent(const ClipboardItem &item);
    void setItemFavorite(const ClipboardItem &item, bool favorite);
    QList<ClipboardItem> allItems();
    bool handleWheelScroll(QWheelEvent *event);

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

signals:
    void doubleClicked(const ClipboardItem &item);
    void plainTextPasteRequested(const ClipboardItem &item);
    void detailsRequested(const ClipboardItem &item);
    void itemCountChanged(int itemCount);
    void itemStared(const ClipboardItem &item);
    void itemUnstared(const ClipboardItem &item);

private slots:
    void itemClicked();
    void itemDoubleClicked();

private:
    ClipboardItemWidget *createItemWidget(const ClipboardItem &item);
    ClipboardItemWidget *findMatchingWidget(const ClipboardItem &item) const;
    void registerWidgetFingerprint(ClipboardItemWidget *widget);
    void unregisterWidgetFingerprint(ClipboardItemWidget *widget);
    void setSelectedItem(ClipboardItemWidget *item);
    QString getItemFilePath(const ClipboardItem &item);
    void setFirstVisibleItemSelected();
    void applyFilters();
    void saveItem(const ClipboardItem &item);
    void checkSaveDir();
    void moveItemToFirst(ClipboardItemWidget *widget);
    QString saveDir();
    void animateScrollTo(int targetValue);
    int wheelStepPixels() const;
    void loadNextBatch(int batchSize);
    void ensureAllItemsLoaded();
    void maybeLoadMoreItems();
    int itemCountForDisplay() const;
    void trimToMaxSize();
    void prepareLoadFromSaveDir();
    void continueDeferredLoad();
    bool shouldKeepDeferredLoading() const;
    void refreshContentWidthHint();
    void updateContentWidthHint();
    void updateEdgeFadeOverlays();
    void scheduleDeferredLoadBatch();
    void handleDeferredBatchRead(const QList<QPair<QString, QByteArray>> &batchItems);
    void processDeferredLoadedItems();
    void waitForDeferredRead();
    bool appendLoadedItem(const QString &filePath, const QByteArray &rawData);

    Ui::ScrollItemsWidget *ui;
    QHBoxLayout *layout;
    QString category;
    QString borderColor;

    ClipboardItemWidget *currItemWidget;
    LocalSaver *saver;
    QPropertyAnimation *scrollAnimation;
    QTimer *deferredLoadTimer_ = nullptr;
    QThread *deferredLoadThread_ = nullptr;
    QWidget *leftEdgeFadeOverlay_ = nullptr;
    QWidget *rightEdgeFadeOverlay_ = nullptr;
    QHash<QByteArray, QList<ClipboardItemWidget*>> fingerprintBuckets_;
    QSet<QByteArray> favoriteFingerprints_;
    QList<QPair<QString, QByteArray>> deferredLoadedItems_;
    QStringList pendingLoadFilePaths_;
    int edgeContentPadding_ = 0;
    int edgeFadeWidth_ = 0;
    int totalItemCount_ = 0;
    bool deferredLoadActive_ = false;

    QString currentKeyword_;
    ClipboardItem::ContentType currentTypeFilter_ = ClipboardItem::All;

    static constexpr int INITIAL_LOAD_BATCH_SIZE = 24;
    static constexpr int LOAD_BATCH_SIZE = 16;
    static constexpr int DEFERRED_LOAD_BATCH_SIZE = 6;
    static constexpr int LOAD_MORE_THRESHOLD_PX = 640;
    static constexpr int HIDDEN_PARSE_SIZE_THRESHOLD = 512 * 1024;

};

#endif // SCROLLITEMSWIDGET_H
