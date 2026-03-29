// input: Depends on Qt Widgets, ClipboardItem data, persistence service, and item widgets.
// output: Exposes one history board with lazy loading, filtering, fixed-page browsing, dedup indexing, and paste signals.
// pos: Widget-layer horizontal board declaration for clipboard/favorites views.
// update: If I change, update this header block and my folder README.md (main-card context menu save/export + multi-select batch actions + diff-based thumbnail visibility tracking + hidden-stage light prewarm).
// note: Added theme application entry point, alias sync hooks, on-demand thumbnail loading with prefetch, loading overlay, diff-based thumbnail visibility tracking, hidden-stage light prewarm, and switchable paged-vs-continuous history loading.
#ifndef SCROLLITEMSWIDGET_H
#define SCROLLITEMSWIDGET_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QModelIndex>
#include <QPair>
#include <QPersistentModelIndex>
#include <QSet>
#include <QWidget>

#include "utils/ClipboardBoardService.h"

#include "data/ClipboardItem.h"
#include "ClipboardBoardModel.h"

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
class QLabel;

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
    void mergeDeferredMimeFormats(const QString &itemName, const QMap<QString, QByteArray> &extraFormats);
    void filterByKeyword(const QString &keyword);
    void filterByType(ContentType type);
    void setShortcutInfo();
    void cleanShortCutInfo();
    void loadFromSaveDir();
    void loadFromSaveDirDeferred();
    void syncFromDiskIncremental();
    void applyScale(int scale);
    QScrollBar* horizontalScrollbar() const;
    void setAllItemVisible();
    const ClipboardItem* currentSelectedItem() const;
    const ClipboardItem* selectedByShortcut(int visibleOrder);
    const ClipboardItem* selectedByEnter();
    int selectedItemCount() const;
    bool hasMultipleSelectedItems() const;
    void hideHoverTools();
    void focusMoveLeft();
    void focusMoveRight();
    int getItemCount();
    void setCurrentPageNumber(int pageNumber);
    int currentPageNumber() const;
    int totalPageCount() const;
    void refreshThumbnailCache();
    QSet<QByteArray> loadAllFingerprints();
    void setFavoriteFingerprints(const QSet<QByteArray> &fingerprints);

    void moveSelectedToFirst();
    void scrollToFirst();
    void scrollToLast();
    QString getCategory() const;
    void removeItemByContent(const ClipboardItem &item);
    void setItemFavorite(const ClipboardItem &item, bool favorite);
    void syncAlias(const QByteArray &fingerprint, const QString &alias);
    QList<ClipboardItem> allItems();
    bool handleWheelScroll(QWheelEvent *event);
    void applyTheme(bool dark);
    ClipboardBoardService *boardServiceRef() const { return boardService_; }
    QString memoryStats() const;

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
    void aliasChanged(const QByteArray &fingerprint, const QString &alias);
    void localPersistenceChanged();
    void selectionStateChanged();
    void pageStateChanged(int currentPage, int totalPages);

private slots:
    void handleCurrentIndexChanged(const QModelIndex &current, const QModelIndex &previous);
    void handleActivatedIndex(const QModelIndex &index);
    void showContextMenu(const QPoint &pos);
    void handleLoadedItems(const QList<QPair<QString, ClipboardItem>> &items);
    void handlePendingItemReady(const QString &expectedName, const ClipboardItem &item);
    void handleThumbnailReady(const QString &expectedName, const QPixmap &thumbnail);
    void handleKeywordMatched(const QSet<QString> &matchedNames, quint64 token);
    void handleTotalItemCountChanged(int total);
    void handleDeferredLoadCompleted();
    void preRenderAndCleanup();

private:
    QModelIndex currentProxyIndex() const;
    QModelIndex proxyIndexForSourceRow(int sourceRow) const;
    int sourceRowForProxyIndex(const QModelIndex &proxyIndex) const;
    QList<QModelIndex> selectedProxyIndexes() const;
    QList<int> selectedSourceRows() const;
    QList<ClipboardItem> selectedItems() const;
    void setCurrentProxyIndex(const QModelIndex &index);
    void setFirstVisibleItemSelected();
    void applyFilters();
    int totalItemCountForPagination() const;
    bool paginationEnabled() const;
    bool shouldEvictPages() const;
    bool filterShowsVisualPreviewCards() const;
    bool usesManagedVisualPreviewCard(const ClipboardItem &item) const;
    ClipboardBoardModel::PreviewState previewStateForItem(const ClipboardItem &item) const;
    void syncPreviewStateForRow(int row);
    void syncPreviewStateForName(const QString &name);
    void releaseManagedVisualThumbnailsFromModel();
    void reloadAllIndexedItems();
    void reloadCurrentPageItems(bool resetSelection);
    bool shouldReloadCurrentPage(int totalItems) const;
    void ensureCurrentPageLoaded(bool resetSelection);
    void syncPageWindow(bool resetSelection = false);
    int moveItemToFirst(int sourceRow);
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
    void populateSingleSelectionMenu(QMenu *menu, const QModelIndex &proxyIndex, const ClipboardItem &item);
    void populateMultiSelectionMenu(QMenu *menu, const QList<ClipboardItem> &selection);
    void updateHoverActionBar(const QModelIndex &proxyIndex);
    void updateHoverActionBarPosition();
    void hideHoverActionBar(bool animated = true);
    void updateHoverFavoriteButton(bool favorite);
    void updateHoverPinButton(bool pinned);
    void openAliasDialogForItem(const ClipboardItem &item);
    void startAsyncKeywordSearch();
    void appendModelItem(const ClipboardItem &item);
    bool appendLoadedItem(const QString &filePath, const ClipboardItem &item);
    void removeItems(const QList<ClipboardItem> &items);
    void applyFavoriteToItems(const QList<ClipboardItem> &items, bool favorite);
    QList<QModelIndex> shortcutVisibleIndexes() const;
    int pinnedInsertRow() const;
    int unpinnedInsertRowForItem(const ClipboardItem &item, int excludeRow) const;
    void setItemPinned(const ClipboardItem &item, bool pinned);
    void saveItemToFile(const ClipboardItem &item);
    QPair<int, int> displaySequenceForIndex(const QModelIndex &proxyIndex) const;
    int selectedSourceRow() const;
    const ClipboardItem *cacheSelectedItem(int sourceRow) const;
    bool isBoardUiVisible() const;
    void scheduleThumbnailUpdate();
    void primeVisibleThumbnailsSync();
    void updateVisibleThumbnails();
    bool shouldManageThumbnail(const ClipboardItem &item) const;
    void requestThumbnailForItem(const ClipboardItem &item);
    void applyManagedThumbnailNames(const QSet<QString> &desiredNames);
    void setVisibleLoadingThumbnailNames(const QSet<QString> &names);
    void updateThumbnailViewport(const QSet<QString> &names);
    void updateLoadingOverlay();
    void updateSelectionState();
    void releaseItemPixmaps(int row);

    Ui::ScrollItemsWidget *ui;
    QString category;
    QString borderColor;

    ClipboardBoardService *boardService_ = nullptr;
    QListView *listView_ = nullptr;
    ClipboardBoardModel *boardModel_ = nullptr;
    ClipboardBoardProxyModel *filterProxyModel_ = nullptr;
    ClipboardBoardProxyModel *proxyModel_ = nullptr;
    ClipboardCardDelegate *cardDelegate_ = nullptr;
    QPropertyAnimation *scrollAnimation;
    QWidget *leftEdgeFadeOverlay_ = nullptr;
    QWidget *rightEdgeFadeOverlay_ = nullptr;
    QWidget *hoverActionBar_ = nullptr;
    QToolButton *hoverDetailsBtn_ = nullptr;
    QToolButton *hoverAliasBtn_ = nullptr;
    QToolButton *hoverPinBtn_ = nullptr;
    QToolButton *hoverFavoriteBtn_ = nullptr;
    QToolButton *hoverDeleteBtn_ = nullptr;
    QGraphicsOpacityEffect *hoverOpacity_ = nullptr;
    QLabel *loadingLabel_ = nullptr;
    QPersistentModelIndex hoverProxyIndex_;
    QTimer *hoverHideTimer_ = nullptr;
    QSet<QByteArray> favoriteFingerprints_;
    QSet<QString> pendingLinkPreviewUrls_;
    QSet<QString> pendingThumbnailNames_;
    QSet<QString> missingThumbnailNames_;
    QSet<QString> desiredThumbnailNames_;
    QSet<QString> managedThumbnailNames_;
    QSet<QString> visibleLoadingThumbnailNames_;
    QTimer *thumbnailUpdateTimer_ = nullptr;
    QTimer *thumbnailPulseTimer_ = nullptr;
    int thumbnailLoadingPhase_ = 0;
    int edgeContentPadding_ = 0;
    int edgeFadeWidth_ = 0;
    quint64 keywordSearchToken_ = 0;
    QSet<QString> asyncKeywordMatchedNames_;
    mutable ClipboardItem selectedItemCache_;

    QString currentKeyword_;
    ContentType currentTypeFilter_ = All;
    bool darkTheme_ = false;
    int currentPage_ = 0;
    int pageBaseOffset_ = 0;
    int loadedPage_ = -1;
    int loadedPageBaseOffset_ = -1;
    int loadedPageTotalItems_ = -1;
    ContentType loadedPageTypeFilter_ = All;
    QString loadedPageKeyword_;
    QSet<QString> loadedPageMatchedNames_;

    static constexpr int PAGE_SIZE = 50;
    static constexpr int PAGED_INITIAL_LOAD_BATCH_SIZE = PAGE_SIZE;
    static constexpr int CONTINUOUS_INITIAL_LOAD_BATCH_SIZE = 24;
    static constexpr int LOAD_BATCH_SIZE = 16;
    static constexpr int PAGE_LOAD_BATCH_SIZE = PAGE_SIZE;
    static constexpr int CONTINUOUS_DEFERRED_LOAD_BATCH_SIZE = 6;
    static constexpr int LOAD_MORE_THRESHOLD_PX = 640;
};

#endif // SCROLLITEMSWIDGET_H
