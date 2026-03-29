// input: Depends on ScrollItemsWidget.h, ClipboardBoardService, Qt model/view.
// output: Implements pagination and filtering methods of ScrollItemsWidget.
// pos: Split from ScrollItemsWidgetMV.cpp -- page management, filter application, item counts.
#include <QItemSelectionModel>
#include <QListView>
#include <QScrollBar>

#include <utils/MPasteSettings.h>

#include "ClipboardBoardModel.h"
#include "ClipboardBoardProxyModel.h"
#include "ClipboardCardDelegate.h"
#include "ScrollItemsWidget.h"

bool ScrollItemsWidget::paginationEnabled() const {
    return MPasteSettings::getInst()->getHistoryViewMode() == MPasteSettings::ViewModePaged;
}

bool ScrollItemsWidget::shouldEvictPages() const {
    return paginationEnabled();
}

int ScrollItemsWidget::totalItemCountForPagination() const {
    if (paginationEnabled()) {
        return boardService_
            ? boardService_->filteredItemCount(currentTypeFilter_, currentKeyword_, asyncKeywordMatchedNames_)
            : 0;
    }
    if (!currentKeyword_.isEmpty() || currentTypeFilter_ != All) {
        return filterProxyModel_ ? filterProxyModel_->rowCount() : 0;
    }
    return boardService_ ? boardService_->totalItemCount()
                         : (filterProxyModel_ ? filterProxyModel_->rowCount() : 0);
}

int ScrollItemsWidget::itemCountForDisplay() const {
    return totalItemCountForPagination();
}

void ScrollItemsWidget::syncPageWindow(bool resetSelection) {
    if (!proxyModel_) {
        return;
    }

    if (!paginationEnabled()) {
        currentPage_ = 0;
        pageBaseOffset_ = 0;
        loadedPage_ = -1;
        loadedPageBaseOffset_ = -1;
        loadedPageTotalItems_ = -1;
        loadedPageTypeFilter_ = All;
        loadedPageKeyword_.clear();
        loadedPageMatchedNames_.clear();
        proxyModel_->setPageSize(0);
        proxyModel_->setPageIndex(0);
        if (listView_ && resetSelection) {
            setFirstVisibleItemSelected();
        }
        emit pageStateChanged(0, 0);
        return;
    }

    const int totalItems = totalItemCountForPagination();
    const int totalPages = totalItems > 0 ? ((totalItems + PAGE_SIZE - 1) / PAGE_SIZE) : 0;
    const int clampedPage = totalPages > 0 ? qBound(0, currentPage_, totalPages - 1) : 0;
    if (currentPage_ != clampedPage) {
        currentPage_ = clampedPage;
    }

    if (!shouldEvictPages()) {
        pageBaseOffset_ = 0;
    }

    proxyModel_->setPageSize(shouldEvictPages() ? 0 : PAGE_SIZE);
    ensureCurrentPageLoaded(resetSelection);
    proxyModel_->setPageIndex(shouldEvictPages() ? 0 : currentPage_);

    if (listView_ && resetSelection) {
        setFirstVisibleItemSelected();
        QScrollBar *scrollBar = horizontalScrollbar();
        if (scrollBar) {
            scrollBar->setValue(scrollBar->minimum());
        }
    }

    emit pageStateChanged(currentPageNumber(), totalPages);
}

bool ScrollItemsWidget::shouldReloadCurrentPage(int totalItems) const {
    if (!paginationEnabled() || !boardModel_) {
        return false;
    }

    const int totalPages = totalItems > 0 ? ((totalItems + PAGE_SIZE - 1) / PAGE_SIZE) : 0;
    const int clampedPage = totalPages > 0 ? qBound(0, currentPage_, totalPages - 1) : 0;
    const int targetOffset = totalPages > 0 ? clampedPage * PAGE_SIZE : 0;
    const int targetCount = qMin(PAGE_SIZE, qMax(0, totalItems - targetOffset));

    return loadedPage_ != clampedPage
        || loadedPageBaseOffset_ != targetOffset
        || loadedPageTypeFilter_ != currentTypeFilter_
        || loadedPageKeyword_ != currentKeyword_
        || loadedPageMatchedNames_ != asyncKeywordMatchedNames_
        || boardModel_->rowCount() != targetCount;
}

void ScrollItemsWidget::ensureCurrentPageLoaded(bool resetSelection) {
    if (!paginationEnabled() || !boardService_ || !boardModel_) {
        return;
    }

    if (shouldEvictPages()) {
        const int totalItems = totalItemCountForPagination();
        if (shouldReloadCurrentPage(totalItems)) {
            reloadCurrentPageItems(resetSelection);
        } else if (loadedPageTotalItems_ != totalItems) {
            // Total count changed but current page content is identical --
            // just update the cached count to avoid a full reload.
            loadedPageTotalItems_ = totalItems;
        }
        return;
    }

    const int totalItems = totalItemCountForPagination();
    const int requiredCount = qMin(totalItems, (currentPage_ + 1) * PAGE_SIZE);
    while (boardModel_->rowCount() < requiredCount && boardService_->hasPendingItems()) {
        boardService_->loadNextBatch(PAGE_LOAD_BATCH_SIZE);
    }
}

void ScrollItemsWidget::reloadCurrentPageItems(bool resetSelection) {
    if (!boardService_ || !boardModel_) {
        return;
    }

    QString previousSelectedName;
    if (!resetSelection) {
        if (const ClipboardItem *selectedItem = currentSelectedItem()) {
            previousSelectedName = selectedItem->getName();
        }
    }

    if (cardDelegate_) {
        cardDelegate_->clearIntermediateCaches();
    }

    const int totalItems = totalItemCountForPagination();
    const int totalPages = totalItems > 0 ? ((totalItems + PAGE_SIZE - 1) / PAGE_SIZE) : 0;
    const int clampedPage = totalPages > 0 ? qBound(0, currentPage_, totalPages - 1) : 0;
    currentPage_ = clampedPage;
    pageBaseOffset_ = totalPages > 0 ? currentPage_ * PAGE_SIZE : 0;

    // Load with thumbnails so cards can render immediately without
    // waiting for the async thumbnail management cycle.
    const QList<QPair<QString, ClipboardItem>> items =
        boardService_->loadFilteredIndexedSlice(currentTypeFilter_,
                                                currentKeyword_,
                                                asyncKeywordMatchedNames_,
                                                pageBaseOffset_,
                                                PAGE_SIZE,
                                                true);

    pendingThumbnailNames_.clear();
    missingThumbnailNames_.clear();
    desiredThumbnailNames_.clear();
    managedThumbnailNames_.clear();
    visibleLoadingThumbnailNames_.clear();
    selectedItemCache_ = ClipboardItem();
    boardModel_->clear();

    for (const auto &payload : items) {
        appendModelItem(payload.second);
        // Register loaded thumbnails so updateVisibleThumbnails can
        // properly compute the leaving set and unload distant ones.
        if (payload.second.hasThumbnail() && shouldManageThumbnail(payload.second)) {
            managedThumbnailNames_.insert(payload.second.getName());
        }
    }

    loadedPage_ = currentPage_;
    loadedPageBaseOffset_ = pageBaseOffset_;
    loadedPageTotalItems_ = totalItems;
    loadedPageTypeFilter_ = currentTypeFilter_;
    loadedPageKeyword_ = currentKeyword_;
    loadedPageMatchedNames_ = asyncKeywordMatchedNames_;

    if (listView_) {
        if (!resetSelection && !previousSelectedName.isEmpty()) {
            const int selectedRow = boardModel_->rowForName(previousSelectedName);
            if (selectedRow >= 0) {
                setCurrentProxyIndex(proxyIndexForSourceRow(selectedRow));
                return;
            }
        }

        if (resetSelection) {
            setFirstVisibleItemSelected();
            QScrollBar *scrollBar = horizontalScrollbar();
            if (scrollBar) {
                scrollBar->setValue(scrollBar->minimum());
            }
        } else if (listView_->selectionModel()) {
            listView_->selectionModel()->clearCurrentIndex();
        }
    }
}

void ScrollItemsWidget::reloadAllIndexedItems() {
    if (!boardService_ || !boardModel_) {
        return;
    }

    if (paginationEnabled() && cardDelegate_) {
        cardDelegate_->clearVisualCaches();
    }

    const QList<QPair<QString, ClipboardItem>> items =
        boardService_->loadIndexedSlice(0, boardService_->totalItemCount());

    pendingThumbnailNames_.clear();
    missingThumbnailNames_.clear();
    desiredThumbnailNames_.clear();
    managedThumbnailNames_.clear();
    visibleLoadingThumbnailNames_.clear();
    selectedItemCache_ = ClipboardItem();
    pageBaseOffset_ = 0;
    loadedPage_ = -1;
    loadedPageBaseOffset_ = -1;
    loadedPageTotalItems_ = -1;
    loadedPageTypeFilter_ = All;
    loadedPageKeyword_.clear();
    loadedPageMatchedNames_.clear();
    boardModel_->clear();

    for (const auto &payload : items) {
        appendModelItem(payload.second);
    }
}

void ScrollItemsWidget::setCurrentPageNumber(int pageNumber) {
    if (!paginationEnabled()) {
        return;
    }

    const int totalPages = totalPageCount();
    const int requestedPage = qMax(1, pageNumber);
    const int clampedPage = totalPages > 0 ? qBound(1, requestedPage, totalPages) : 1;
    const int newPageIndex = qMax(0, clampedPage - 1);
    if (currentPage_ == newPageIndex && proxyModel_ && proxyModel_->pageIndex() == newPageIndex) {
        return;
    }

    currentPage_ = newPageIndex;
    syncPageWindow(true);
    if (isBoardUiVisible()) {
        refreshContentWidthHint();
        primeVisibleThumbnailsSync();
        scheduleThumbnailUpdate();
    }
}

int ScrollItemsWidget::currentPageNumber() const {
    if (!paginationEnabled()) {
        return 0;
    }
    return totalPageCount() > 0 ? (currentPage_ + 1) : 0;
}

int ScrollItemsWidget::totalPageCount() const {
    if (!paginationEnabled()) {
        return 0;
    }
    const int totalItems = totalItemCountForPagination();
    return totalItems > 0 ? ((totalItems + PAGE_SIZE - 1) / PAGE_SIZE) : 0;
}

void ScrollItemsWidget::applyFilters() {
    if (!filterShowsVisualPreviewCards()) {
        releaseManagedVisualThumbnailsFromModel();
    }
    if (!filterShowsVisualPreviewCards() && cardDelegate_) {
        cardDelegate_->clearVisualCaches();
    }

    if (!paginationEnabled()
        && (!currentKeyword_.isEmpty() || currentTypeFilter_ != All)
        && boardService_) {
        if (boardService_->hasPendingItems()) {
            ensureAllItemsLoaded();
        }
    }

    if (filterProxyModel_) {
        filterProxyModel_->setTypeFilter(paginationEnabled() ? All : currentTypeFilter_);
        filterProxyModel_->setKeyword(paginationEnabled() ? QString() : currentKeyword_);
        filterProxyModel_->setAsyncMatchedNames(paginationEnabled() ? QSet<QString>() : asyncKeywordMatchedNames_);
    }

    currentPage_ = 0;
    syncPageWindow(true);
    if (isBoardUiVisible()) {
        refreshContentWidthHint();
        primeVisibleThumbnailsSync();
        scheduleThumbnailUpdate();
    }
    emit itemCountChanged(itemCountForDisplay());
}
