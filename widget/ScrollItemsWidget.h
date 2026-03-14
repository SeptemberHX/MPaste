// input: Depends on Qt Widgets, ClipboardItem data, persistence service, and item widgets.
// output: Exposes one history board with lazy loading, filtering, dedup indexing, and paste signals.
// pos: Widget-layer horizontal board declaration for clipboard/favorites views.
// update: If I change, update this header block and my folder README.md.
// note: Added theme application entry point.
#ifndef SCROLLITEMSWIDGET_H
#define SCROLLITEMSWIDGET_H

#include <QByteArray>
#include <QList>
#include <QModelIndex>
#include <QPair>
#include <QPersistentModelIndex>
#include <QSet>
#include <QWidget>

#include "utils/ClipboardBoardService.h"

#include "data/ClipboardItem.h"

class QPropertyAnimation;
class QModelIndex;
class QResizeEvent;
class QScrollBar;
class QTimer;
class QHideEvent;
class QWheelEvent;
class QShowEvent;
class QListView;
class QGraphicsOpacityEffect;
class QToolButton;

class ClipboardBoardModel;
class ClipboardBoardProxyModel;
class ClipboardCardDelegate;

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
    QScrollBar* horizontalScrollbar() const;
    void setAllItemVisible();
    const ClipboardItem* currentSelectedItem() const;
    const ClipboardItem* selectedByShortcut(int visibleOrder);
    const ClipboardItem* selectedByEnter();
    void hideHoverTools();
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
    void applyTheme(bool dark);

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

signals:
    void doubleClicked(const ClipboardItem &item);
    void plainTextPasteRequested(const ClipboardItem &item);
    void detailsRequested(const ClipboardItem &item, int sequence, int totalCount);
    void previewRequested(const ClipboardItem &item);
    void itemCountChanged(int itemCount);
    void itemStared(const ClipboardItem &item);
    void itemUnstared(const ClipboardItem &item);

private slots:
    void handleCurrentIndexChanged(const QModelIndex &current, const QModelIndex &previous);
    void handleActivatedIndex(const QModelIndex &index);
    void showContextMenu(const QPoint &pos);
    void handleLoadedItems(const QList<QPair<QString, ClipboardItem>> &items);
    void handlePendingItemReady(const QString &expectedName, const ClipboardItem &item);
    void handleKeywordMatched(const QSet<QString> &matchedNames, quint64 token);
    void handleTotalItemCountChanged(int total);
    void handleDeferredLoadCompleted();

private:
    QModelIndex currentProxyIndex() const;
    QModelIndex proxyIndexForSourceRow(int sourceRow) const;
    void setCurrentProxyIndex(const QModelIndex &index);
    void setFirstVisibleItemSelected();
    void applyFilters();
    void moveItemToFirst(int sourceRow);
    void animateScrollTo(int targetValue);
    int wheelStepPixels() const;
    void ensureAllItemsLoaded();
    void maybeLoadMoreItems();
    int itemCountForDisplay() const;
    void trimExpiredItems();
    void prepareLoadFromSaveDir();
    bool shouldKeepDeferredLoading() const;
    void refreshContentWidthHint();
    void updateContentWidthHint();
    void updateEdgeFadeOverlays();
    void ensureLinkPreviewForIndex(const QModelIndex &proxyIndex);
    void createHoverActionBar();
    void updateHoverActionBar(const QModelIndex &proxyIndex);
    void updateHoverActionBarPosition();
    void hideHoverActionBar(bool animated = true);
    void updateHoverFavoriteButton(bool favorite);
    void startAsyncKeywordSearch();
    bool appendLoadedItem(const QString &filePath, const ClipboardItem &item);
    QPair<int, int> displaySequenceForIndex(const QModelIndex &proxyIndex) const;
    int selectedSourceRow() const;
    const ClipboardItem *cacheSelectedItem(int sourceRow) const;

    Ui::ScrollItemsWidget *ui;
    QString category;
    QString borderColor;

    ClipboardBoardService *boardService_ = nullptr;
    QListView *listView_ = nullptr;
    ClipboardBoardModel *boardModel_ = nullptr;
    ClipboardBoardProxyModel *proxyModel_ = nullptr;
    ClipboardCardDelegate *cardDelegate_ = nullptr;
    QPropertyAnimation *scrollAnimation;
    QWidget *leftEdgeFadeOverlay_ = nullptr;
    QWidget *rightEdgeFadeOverlay_ = nullptr;
    QWidget *hoverActionBar_ = nullptr;
    QToolButton *hoverDetailsBtn_ = nullptr;
    QToolButton *hoverFavoriteBtn_ = nullptr;
    QToolButton *hoverDeleteBtn_ = nullptr;
    QGraphicsOpacityEffect *hoverOpacity_ = nullptr;
    QPersistentModelIndex hoverProxyIndex_;
    QTimer *hoverHideTimer_ = nullptr;
    QSet<QByteArray> favoriteFingerprints_;
    QSet<QString> pendingLinkPreviewUrls_;
    int edgeContentPadding_ = 0;
    int edgeFadeWidth_ = 0;
    quint64 keywordSearchToken_ = 0;
    QSet<QString> asyncKeywordMatchedNames_;
    mutable ClipboardItem selectedItemCache_;

    QString currentKeyword_;
    ClipboardItem::ContentType currentTypeFilter_ = ClipboardItem::All;
    bool darkTheme_ = false;

    static constexpr int INITIAL_LOAD_BATCH_SIZE = 24;
    static constexpr int LOAD_BATCH_SIZE = 16;
    static constexpr int DEFERRED_LOAD_BATCH_SIZE = 6;
    static constexpr int LOAD_MORE_THRESHOLD_PX = 640;
};

#endif // SCROLLITEMSWIDGET_H
