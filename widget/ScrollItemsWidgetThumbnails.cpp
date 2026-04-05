// input: Depends on ScrollItemsWidget.h, ClipboardBoardService, Qt model/view APIs.
// output: Implements thumbnail management, visual preview state, and viewport update methods of ScrollItemsWidget.
// pos: Split from ScrollItemsWidgetMV.cpp -- thumbnail lifecycle and preview-state bookkeeping.
#include <QGuiApplication>
#include <QListView>
#include <QScreen>
#include <QScrollBar>
#include <QTimer>

#include "BoardInternalHelpers.h"
#include "BoardViewWidgets.h"
#include "ClipboardBoardModel.h"
#include "ClipboardBoardProxyModel.h"
#include "ClipboardCardDelegate.h"
#include "ScrollItemsWidget.h"

using namespace BoardHelpers;

void ScrollItemsWidget::scheduleThumbnailUpdate() {
    // No-op: cardPixmapCache_ manages all rendering.
}

int ScrollItemsWidget::estimateVisibleCardCount() const {
    if (!listView_) {
        return 0;
    }
    const int gridWidth = qMax(1, listView_->gridSize().width());
    int vpWidth = listView_->viewport() ? listView_->viewport()->width() : 0;
    // When the window is hidden the viewport is tiny; fall back to
    // primary screen width so startup pre-render covers enough cards.
    if (vpWidth < gridWidth) {
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            vpWidth = screen->availableSize().width();
        } else {
            vpWidth = 1920;
        }
    }
    return qMax(1, vpWidth / gridWidth + 2);
}

void ScrollItemsWidget::requestVisibleThumbnails() {
    if (!boardService_ || !boardModel_ || !proxyModel_ || !listView_ || !isBoardUiVisible()) {
        return;
    }

    const int proxyCount = proxyModel_->rowCount();
    if (proxyCount <= 0) {
        return;
    }

    const int visibleCount = estimateVisibleCardCount();
    const QRect viewportRect = listView_->viewport()->rect();
    const QModelIndex leftIndex = listView_->indexAt(QPoint(viewportRect.left() + 1, viewportRect.center().y()));
    const int startRow = leftIndex.isValid() ? leftIndex.row() : 0;
    const int endRow = qMin(proxyCount - 1, startRow + visibleCount - 1);

    for (int proxyRow = startRow; proxyRow <= endRow; ++proxyRow) {
        const QModelIndex proxyIndex = proxyModel_->index(proxyRow, 0);
        const int sourceRow = sourceRowForProxyIndex(proxyIndex);
        if (sourceRow < 0) {
            continue;
        }

        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item || !shouldManageThumbnail(*item) || item->hasThumbnail() || !item->hasThumbnailHint()) {
            continue;
        }

        const QString filePath = item->sourceFilePath().isEmpty()
            ? boardService_->filePathForName(item->getName())
            : item->sourceFilePath();
        if (filePath.isEmpty()) {
            missingThumbnailNames_.insert(item->getName());
            syncPreviewStateForRow(sourceRow);
            continue;
        }

        // Load thumbnails asynchronously so the showEvent path is not
        // blocked by disk I/O.  Cards show a loading placeholder until
        // the thumbnailReady signal delivers the pixmap.
        desiredThumbnailNames_.insert(item->getName());
        requestThumbnailForItem(*item);
    }
}

bool ScrollItemsWidget::shouldManageThumbnail(const ClipboardItem &item) const {
    const ContentType type = item.getContentType();
    return type == Image
        || type == Link
        || type == Office
        || (type == RichText && item.getPreviewKind() == VisualPreview);
}

void ScrollItemsWidget::requestThumbnailForItem(const ClipboardItem &item) {
    if (!boardService_ || item.getName().isEmpty() || item.sourceFilePath().isEmpty()) {
        return;
    }
    if (missingThumbnailNames_.contains(item.getName())) {
        return;
    }
    if (pendingThumbnailNames_.contains(item.getName())) {
        return;
    }
    pendingThumbnailNames_.insert(item.getName());
    syncPreviewStateForName(item.getName());
    boardService_->requestThumbnailAsync(item.getName(), item.sourceFilePath());
}

void ScrollItemsWidget::applyManagedThumbnailNames(const QSet<QString> &desiredNames) {
    if (!boardModel_) {
        managedThumbnailNames_.clear();
        return;
    }

    QSet<QString> leavingNames = managedThumbnailNames_;
    leavingNames.subtract(desiredNames);

    for (const QString &name : leavingNames) {
        const int row = boardModel_->rowForName(name);
        if (row < 0) {
            continue;
        }
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (!item || !shouldManageThumbnail(*item) || !item->hasThumbnail() || item->sourceFilePath().isEmpty()) {
            continue;
        }
        ClipboardItem updated = *item;
        updated.setThumbnail(QPixmap());
        boardModel_->updateItem(row, updated);
    }

    for (const QString &name : desiredNames) {
        const int row = boardModel_->rowForName(name);
        if (row < 0) {
            continue;
        }
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (item && shouldManageThumbnail(*item) && !item->hasThumbnail()) {
            requestThumbnailForItem(*item);
        }
    }

    managedThumbnailNames_ = desiredNames;
    desiredThumbnailNames_ = desiredNames;
}

void ScrollItemsWidget::setVisibleLoadingThumbnailNames(const QSet<QString> &names) {
    QSet<QString> changedNames = visibleLoadingThumbnailNames_;
    changedNames.unite(names);
    visibleLoadingThumbnailNames_ = names;
    updateThumbnailViewport(changedNames);

    if (!thumbnailPulseTimer_) {
        return;
    }

    if (visibleLoadingThumbnailNames_.isEmpty()) {
        thumbnailPulseTimer_->stop();
        return;
    }

    if (!thumbnailPulseTimer_->isActive()) {
        thumbnailPulseTimer_->start();
    }
}

void ScrollItemsWidget::updateThumbnailViewport(const QSet<QString> &names) {
    if (!listView_ || !boardModel_ || !proxyModel_ || names.isEmpty()) {
        return;
    }

    QWidget *viewport = listView_->viewport();
    if (!viewport) {
        return;
    }

    const QRect viewportRect = viewport->rect();
    for (const QString &name : names) {
        const int sourceRow = boardModel_->rowForName(name);
        if (sourceRow < 0) {
            continue;
        }

        const QModelIndex proxyIndex = proxyIndexForSourceRow(sourceRow);
        if (!proxyIndex.isValid()) {
            continue;
        }

        const QRect rect = listView_->visualRect(proxyIndex);
        if (!rect.isValid() || !viewportRect.intersects(rect)) {
            continue;
        }
        viewport->update(rect.adjusted(-2, -2, 2, 2));
    }
}

void ScrollItemsWidget::releaseItemPixmaps(int row) {
    if (boardModel_) {
        boardModel_->releaseItemPixmaps(row);
    }
}

void ScrollItemsWidget::preRenderAndCleanup() {
    if (!boardModel_ || !cardDelegate_ || !listView_ || !proxyModel_) {
        return;
    }

    managedThumbnailNames_.clear();
    cardDelegate_->clearIntermediateCaches();
    setVisibleLoadingThumbnailNames({});
}

void ScrollItemsWidget::updateVisibleThumbnails() {
    // No-op: cardPixmapCache_ holds all rendered cards.
    // Intermediate data cleanup happens in preRenderAndCleanup().

}

void ScrollItemsWidget::updateContentWidthHint() {
    if (!isBoardUiVisible()) {
        return;
    }
    if (listView_) {
        if (auto *boardView = dynamic_cast<ClipboardBoardView *>(listView_)) {
            boardView->refreshItemGeometries();
        }
        listView_->viewport()->update();
    }
}

bool ScrollItemsWidget::filterShowsVisualPreviewCards() const {
    return currentTypeFilter_ == All
        || currentTypeFilter_ == Image
        || currentTypeFilter_ == Link
        || currentTypeFilter_ == Office
        || currentTypeFilter_ == RichText;
}

bool ScrollItemsWidget::usesManagedVisualPreviewCard(const ClipboardItem &item) const {
    const ContentType type = item.getContentType();
    if (type == Image) {
        return true;
    }
    if (type == Office) {
        // Office items with text but no thumbnail hint (e.g. MathType)
        // should use text preview instead of waiting for a visual thumbnail.
        if (!item.hasThumbnailHint() && !item.getNormalizedText().isEmpty()) {
            return false;
        }
        return true;
    }
    return type == RichText && item.getPreviewKind() == VisualPreview;
}

ClipboardBoardModel::PreviewState ScrollItemsWidget::previewStateForItem(const ClipboardItem &item) const {
    if (!usesManagedVisualPreviewCard(item)) {
        return ClipboardBoardModel::PreviewNotApplicable;
    }
    if (item.hasThumbnail()) {
        return ClipboardBoardModel::PreviewReady;
    }
    if (missingThumbnailNames_.contains(item.getName())) {
        return ClipboardBoardModel::PreviewUnavailable;
    }
    return ClipboardBoardModel::PreviewLoading;
}

void ScrollItemsWidget::syncPreviewStateForRow(int row) {
    if (!boardModel_ || row < 0) {
        return;
    }
    boardModel_->setPreviewState(row, previewStateForItem(boardModel_->itemAt(row)));
}

void ScrollItemsWidget::syncPreviewStateForName(const QString &name) {
    if (!boardModel_ || name.isEmpty()) {
        return;
    }
    syncPreviewStateForRow(boardModel_->rowForName(name));
}

void ScrollItemsWidget::releaseManagedVisualThumbnailsFromModel() {
    if (!boardModel_) {
        return;
    }

    pendingThumbnailNames_.clear();
    desiredThumbnailNames_.clear();
    managedThumbnailNames_.clear();
    visibleLoadingThumbnailNames_.clear();

    const int rowCount = boardModel_->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        ClipboardItem item = boardModel_->itemAt(row);
        if (!usesManagedVisualPreviewCard(item)) {
            continue;
        }
        if (item.hasThumbnail()) {
            item.setThumbnail(QPixmap());
            boardModel_->updateItem(row, item);
        }
        syncPreviewStateForRow(row);
    }
}
