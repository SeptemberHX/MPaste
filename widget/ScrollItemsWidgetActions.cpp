// input: Depends on ScrollItemsWidget.h, ClipboardBoardService, ClipboardBoardActionService, Qt model/view APIs.
// output: Implements item add/remove/move/pin/favorite/alias actions for ScrollItemsWidget.
// pos: Split from ScrollItemsWidgetMV.cpp -- item mutation and reordering actions.
#include <QDateTime>
#include <QFileInfo>
#include <QListView>
#include <QScrollBar>

#include <utils/MPasteSettings.h>

#include "BoardInternalHelpers.h"
#include "data/LocalSaver.h"
#include "ClipboardBoardActionService.h"
#include "ClipboardBoardModel.h"
#include "ClipboardBoardProxyModel.h"
#include "ClipboardCardDelegate.h"
#include "ClipboardItemRenameDialog.h"
#include "ScrollItemsWidget.h"

using namespace BoardHelpers;

bool ScrollItemsWidget::addOneItem(const ClipboardItem &nItem) {
    int row = boardModel_->rowForMatchingItem(nItem);
    if (row < 0) {
        row = boardModel_->rowForFingerprint(nItem.fingerprint());
    }

    if (row >= 0) {
        const ClipboardItem existingItem = boardModel_->itemAt(row);

        // Detect whether the incoming copy carries better data than what
        // is already stored (e.g. the existing item was saved with a bug
        // that dropped MIME content, or the new copy has richer rich-text).
        const bool richerIncomingRichText = nItem.getContentType() == RichText
            && existingItem.getContentType() == RichText
            && nItem.getNormalizedText().size() > existingItem.getNormalizedText().size();
        const bool contentTypeUpgrade = nItem.getContentType() != existingItem.getContentType()
            && existingItem.getContentType() == Text
            && nItem.getContentType() != Text;
        // Detect corrupted items: correct type but missing actual content
        // (e.g. Image item saved without image data due to earlier bug).
        const bool existingLacksContent = !existingItem.hasThumbnail()
            && existingItem.getNormalizedText().trimmed().isEmpty()
            && existingItem.getContentType() != Color;

        if (richerIncomingRichText || contentTypeUpgrade || existingLacksContent) {
            ClipboardItem updated = nItem;
            updated.setPinned(existingItem.isPinned());
            boardModel_->updateItem(row, updated);
            syncPreviewStateForRow(row);
            if (boardService_) {
                boardService_->processPendingItemAsync(updated, updated.getName());
            }
        }
        const bool alreadyFirst = (row == 0) && (currentPage_ == 0);
        if (!alreadyFirst) {
            moveItemToFirst(row);
        }
        return false;
    }

    const bool favorite = category == MPasteSettings::STAR_CATEGORY_NAME
        || favoriteFingerprints_.contains(nItem.fingerprint());
    const int insertRow = pinnedInsertRow();
    boardModel_->insertItem(insertRow, nItem, favorite);

    // Keep the model within PAGE_SIZE when pagination is active.
    if (paginationEnabled() && boardModel_->rowCount() > PAGE_SIZE) {
        boardModel_->removeItemAt(boardModel_->rowCount() - 1);
    }

    const QModelIndex firstProxyIndex = proxyIndexForSourceRow(insertRow);
    // Only auto-select the new item if the user is already at the
    // beginning.  Otherwise scrolling would jump back to the start
    // every time a clipboard capture arrives.
    const QScrollBar *sb = horizontalScrollbar();
    const bool atStart = !sb || sb->value() <= sb->minimum();
    if (atStart) {
        setCurrentProxyIndex(firstProxyIndex);
    }
    ensureLinkPreviewForIndex(firstProxyIndex);
    scheduleThumbnailUpdate();

    if (boardService_) {
        if (!shouldEvictPages()) {
            boardService_->notifyItemAdded();
        }
    }
    trimExpiredItems();
    refreshContentWidthHint();
    emit itemCountChanged(itemCountForDisplay());
    return true;
}

bool ScrollItemsWidget::addAndSaveItem(const ClipboardItem &nItem) {
    // Fast duplicate check before any expensive preparation.
    if (boardModel_) {
        int existingRow = boardModel_->rowForMatchingItem(nItem);
        if (existingRow < 0) {
            existingRow = boardModel_->rowForFingerprint(nItem.fingerprint());
        }
        if (existingRow >= 0) {
            // Delegate to addOneItem which handles update + moveToFirst.
            addOneItem(nItem);
            return false;
        }
    }

    // Also check the on-disk index — during startup the model may not
    // be fully loaded yet, so the model check above can miss duplicates.
    // However, if the model doesn't have the item (existingRow < 0 above)
    // but the on-disk index claims the fingerprint exists, the backing file
    // may have been cleaned up.  In that case, re-add and re-save the item
    // so it becomes visible again.
    if (boardService_ && boardService_->containsFingerprint(nItem.fingerprint())) {
        if (boardModel_ && boardModel_->rowForFingerprint(nItem.fingerprint()) >= 0) {
            return false;
        }
        // Fingerprint in disk index but not in model — treat as new item.
    }

    // Add item to model immediately so the UI can display it right away.
    const bool added = addOneItem(nItem);
    ensureLinkPreviewForIndex(proxyIndexForSourceRow(0));
    if (added && boardService_) {
        // Save + thumbnail generation happen asynchronously to avoid
        // blocking the UI with large items (e.g. Word documents).
        boardService_->processPendingItemAsync(nItem, nItem.getName());

        // Keep loaded-page bookkeeping in sync.
        if (paginationEnabled() && loadedPageTotalItems_ >= 0) {
            loadedPageTotalItems_ = totalItemCountForPagination();
        }
    }
    return added;
}

void ScrollItemsWidget::removeItems(const QList<ClipboardItem> &items) {
    if (!boardModel_ || items.isEmpty()) {
        return;
    }

    for (const ClipboardItem &item : items) {
        if (item.getName().isEmpty()) {
            continue;
        }
        pendingThumbnailNames_.remove(item.getName());
        missingThumbnailNames_.remove(item.getName());
        desiredThumbnailNames_.remove(item.getName());
    }

    const int removedCount = ClipboardBoardActionService::removeItems(boardService_, boardModel_, items);
    if (removedCount <= 0) {
        return;
    }

    syncPageWindow(true);
    refreshContentWidthHint();
    emit itemCountChanged(itemCountForDisplay());
}

void ScrollItemsWidget::removeItemByContent(const ClipboardItem &item) {
    if (!item.getName().isEmpty()) {
        pendingThumbnailNames_.remove(item.getName());
        missingThumbnailNames_.remove(item.getName());
        desiredThumbnailNames_.remove(item.getName());
    }

    // Remember the position so we can select the next item after removal.
    const int rowBefore = boardModel_ ? boardModel_->rowForName(item.getName()) : -1;

    if (ClipboardBoardActionService::removeItem(boardService_, boardModel_, item)) {
        // Select the item that now occupies the deleted position (i.e. the
        // next item), or the last item if we deleted the tail.
        if (boardModel_ && rowBefore >= 0) {
            const int nextRow = qMin(rowBefore, boardModel_->rowCount() - 1);
            if (nextRow >= 0) {
                setCurrentProxyIndex(proxyIndexForSourceRow(nextRow));
            }
        }

        if (paginationEnabled() && loadedPageTotalItems_ > 0) {
            loadedPageTotalItems_ = totalItemCountForPagination();
        }
        refreshContentWidthHint();
        emit itemCountChanged(itemCountForDisplay());
        emit pageStateChanged(currentPageNumber(), totalPageCount());
    }
}

void ScrollItemsWidget::applyFavoriteToItems(const QList<ClipboardItem> &items, bool favorite) {
    if (!boardModel_ || items.isEmpty()) {
        return;
    }

    for (const ClipboardItem &item : items) {
        const QList<int> rows = ClipboardBoardActionService::resolveRowsForItems(boardModel_, {item});
        const int row = rows.isEmpty() ? -1 : rows.first();
        if (row < 0) {
            continue;
        }

        const ClipboardItem currentItem = boardModel_->itemAt(row);
        const bool isFavorite = boardModel_->isFavorite(row);
        if (isFavorite == favorite) {
            continue;
        }

        setItemFavorite(currentItem, favorite);
        if (favorite) {
            emit itemStared(currentItem);
        } else {
            emit itemUnstared(currentItem);
        }
    }
}

void ScrollItemsWidget::setItemFavorite(const ClipboardItem &item, bool favorite) {
    if (favorite) {
        favoriteFingerprints_.insert(item.fingerprint());
    } else {
        favoriteFingerprints_.remove(item.fingerprint());
    }
    ClipboardBoardActionService::setFavorite(boardModel_, item, favorite);
}

void ScrollItemsWidget::syncAlias(const QByteArray &fingerprint, const QString &alias) {
    if (!boardModel_ || fingerprint.isEmpty()) {
        return;
    }

    const int rowCount = boardModel_->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (!item || item->fingerprint() != fingerprint) {
            continue;
        }
        ClipboardItem updated = boardModel_->itemAt(row);
        if (updated.getAlias() == alias) {
            continue;
        }
        updated.setAlias(alias);
        boardModel_->updateItem(row, updated);
        if (boardService_) {
            ClipboardBoardActionService::persistItemMetadata(boardService_, boardModel_, row, updated);
        }
    }
}

void ScrollItemsWidget::moveSelectedToFirst() {
    const int sourceRow = selectedSourceRow();
    if (sourceRow >= 0) {
        moveItemToFirst(sourceRow);
    }
}

void ScrollItemsWidget::moveItemByNameToFirst(const QString &itemName) {
    if (itemName.isEmpty() || !boardModel_) {
        return;
    }
    const int row = boardModel_->rowForName(itemName);
    qInfo().noquote() << QStringLiteral("[move-by-name] name=%1 foundRow=%2 rowCount=%3")
        .arg(itemName).arg(row).arg(boardModel_->rowCount());
    if (row >= 0) {
        moveItemToFirst(row);
    }
}

int ScrollItemsWidget::moveItemToFirst(int sourceRow) {
    if (sourceRow < 0 || !boardModel_) {
        return sourceRow;
    }

    ClipboardItem item = boardModel_->itemAt(sourceRow);
    if (item.getName().isEmpty()) {
        return sourceRow;
    }

    const int pinRow = pinnedInsertRow();
    qInfo().noquote() << QStringLiteral("[moveItemToFirst] sourceRow=%1 name=%2 fp=%3 pinnedInsertRow=%4")
        .arg(sourceRow).arg(item.getName()).arg(QString::fromLatin1(item.fingerprint().toHex().left(12))).arg(pinRow);

    // Update the header timestamp in the .mpaste file so the item
    // sorts first on restart (disk loading sorts by header time).
    // No file rename needed — avoids filesystem events, index refresh
    // cascades, and pagination reload conflicts.
    if (boardService_ && !item.isPinned()) {
        item.setTime(QDateTime::currentDateTime());
        const QString filePath = item.sourceFilePath().isEmpty()
            ? boardService_->filePathForName(item.getName())
            : item.sourceFilePath();
        if (!filePath.isEmpty()) {
            LocalSaver saver;
            saver.updateTimestamp(filePath, item.getTime(), item.getName());
        }
        boardModel_->updateItem(sourceRow, item);
        if (cardDelegate_) {
            cardDelegate_->invalidateCard(item.getName());
        }
        boardService_->updateIndexedItemTime(item.getName(), item.getTime());
    }

    // Move in model and service index.
    const int targetRow = item.isPinned() ? 0 : pinnedInsertRow();
    if (boardService_) {
        boardService_->moveIndexedItemToFront(item.getName());
    }
    if (targetRow != sourceRow) {
        boardModel_->moveItemToRow(sourceRow, targetRow);
    }

    if (shouldEvictPages() && currentPage_ != 0) {
        currentPage_ = 0;
        reloadCurrentPageItems(true);
        syncPageWindow(true);
        return 0;
    }
    if (paginationEnabled() && currentPage_ != 0) {
        setCurrentPageNumber(1);
    }

    const QScrollBar *sb = horizontalScrollbar();
    if (!sb || sb->value() <= sb->minimum()) {
        setCurrentProxyIndex(proxyIndexForSourceRow(targetRow));
    }
    return targetRow;
}

void ScrollItemsWidget::setItemPinned(const ClipboardItem &item, bool pinned) {
    if (!boardModel_) {
        return;
    }
    const int row = boardModel_->rowForName(item.getName());
    if (row < 0) {
        return;
    }

    const ClipboardItem updated = boardModel_->itemAt(row);
    if (updated.isPinned() == pinned) {
        return;
    }

    if (shouldEvictPages()) {
        ClipboardItem persisted = updated;
        persisted.setPinned(pinned);
        boardModel_->updateItem(row, persisted);
        if (!ClipboardBoardActionService::persistItemMetadata(boardService_, boardModel_, row, persisted)) {
            return;
        }
        loadedPage_ = -1;
        loadedPageBaseOffset_ = -1;
        loadedPageTotalItems_ = -1;
        syncPageWindow(false);
        refreshContentWidthHint();
        updateHoverActionBar(currentProxyIndex());
        return;
    }

    const int targetRow = pinned ? 0 : unpinnedInsertRowForItem(updated, row);
    if (!ClipboardBoardActionService::applyPinnedState(boardModel_, boardService_, row, targetRow, pinned)) {
        return;
    }
    const QModelIndex targetIndex = proxyIndexForSourceRow(targetRow);
    setCurrentProxyIndex(targetIndex);
    updateHoverActionBar(targetIndex);
    refreshContentWidthHint();
}

void ScrollItemsWidget::openAliasDialogForItem(const ClipboardItem &item) {
    if (!boardModel_) {
        return;
    }
    const int row = boardModel_->rowForName(item.getName());
    if (row < 0) {
        return;
    }
    ClipboardItemRenameDialog dialog(item.getAlias(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    ClipboardItem updated = boardModel_->itemAt(row);
    updated.setAlias(dialog.alias());
    boardModel_->updateItem(row, updated);
    if (boardService_) {
        ClipboardBoardActionService::persistItemMetadata(boardService_, boardModel_, row, updated);
    }
    emit aliasChanged(updated.fingerprint(), updated.getAlias());
}

void ScrollItemsWidget::mergeDeferredMimeFormats(const QString &itemName, const QMap<QString, QByteArray> &extraFormats) {
    if (!boardModel_ || itemName.isEmpty() || extraFormats.isEmpty()) {
        return;
    }

    const int row = boardModel_->rowForName(itemName);
    if (row < 0) {
        // Item not in current model page — just save the extra formats to
        // the item on disk via the board service so they are available when
        // the user pastes.
        if (boardService_) {
            const QString filePath = boardService_->filePathForName(itemName);
            ClipboardItem item = boardService_->loadItemLight(filePath, false);
            if (!item.getName().isEmpty()) {
                item.ensureMimeDataLoaded();
                for (auto it = extraFormats.cbegin(); it != extraFormats.cend(); ++it) {
                    item.setMimeFormat(it.key(), it.value());
                }
                item.reclassifyContentType();
                boardService_->saveItemQuiet(item);
            }
        }
        return;
    }

    ClipboardItem item = boardModel_->itemAt(row);
    for (auto it = extraFormats.cbegin(); it != extraFormats.cend(); ++it) {
        item.setMimeFormat(it.key(), it.value());
    }

    // Re-classify: the deferred formats may include Office-specific
    // markers (e.g. PowerPoint Internal Shapes) that weren't available
    // during the initial lightweight capture.
    item.reclassifyContentType();

    boardModel_->updateItem(row, item);
    syncPreviewStateForRow(row);

    if (cardDelegate_) {
        cardDelegate_->invalidateCard(itemName);
    }

    if (boardService_) {
        boardService_->saveItemQuiet(item);
    }
}

int ScrollItemsWidget::pinnedInsertRow() const {
    if (!boardModel_) {
        return 0;
    }
    const int rowCount = boardModel_->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (!item || !item->isPinned()) {
            return row;
        }
    }
    return rowCount;
}

int ScrollItemsWidget::unpinnedInsertRowForItem(const ClipboardItem &item, int excludeRow) const {
    if (!boardModel_) {
        return 0;
    }
    const QDateTime targetTime = item.getTime();
    const int rowCount = boardModel_->rowCount();
    int pinnedCount = 0;
    int insertRow = rowCount;

    for (int row = 0; row < rowCount; ++row) {
        if (row == excludeRow) {
            continue;
        }
        const ClipboardItem *candidate = boardModel_->itemPtrAt(row);
        if (!candidate) {
            continue;
        }
        if (candidate->isPinned()) {
            ++pinnedCount;
            continue;
        }
        if (!targetTime.isValid() || !candidate->getTime().isValid()) {
            continue;
        }
        if (targetTime >= candidate->getTime()) {
            insertRow = row;
            break;
        }
    }

    if (insertRow < pinnedCount) {
        insertRow = pinnedCount;
    }
    return insertRow;
}

void ScrollItemsWidget::trimExpiredItems() {
    if (category == MPasteSettings::STAR_CATEGORY_NAME || !boardService_) {
        return;
    }

    const QDateTime cutoff = MPasteSettings::getInst()->historyRetentionCutoff();
    const QStringList removedPaths = boardService_->trimExpiredItems(cutoff);
    if (!removedPaths.isEmpty() && boardModel_) {
        for (const QString &path : removedPaths) {
            const QString name = QFileInfo(path).completeBaseName();
            const int row = boardModel_->rowForName(name);
            if (row >= 0) {
                if (cardDelegate_) {
                    cardDelegate_->invalidateCard(name);
                }
                boardModel_->removeItemAt(row);
            }
        }
        emit itemCountChanged(itemCountForDisplay());
    }

    setFirstVisibleItemSelected();
}
