// input: Depends on ClipboardBoardActionService.h, ClipboardBoardModel, ClipboardBoardService, and LocalSaver persistence helpers.
// output: Implements non-visual board item actions shared by widget interaction flows.
// pos: Widget-layer action helper implementation for persistence-aware board mutations.
// update: If I change, update widget/README.md.
#include "ClipboardBoardActionService.h"

#include <algorithm>
#include <QSaveFile>
#include <QFileInfo>
#include <functional>

#include "ClipboardBoardModel.h"
#include "data/LocalSaver.h"
#include "utils/ClipboardBoardService.h"

namespace ClipboardBoardActionService {

namespace {
bool writeUtf8File(const QString &filePath, const QString &contents, QString *errorMessage) {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const QByteArray payload = contents.toUtf8();
    if (file.write(payload) != payload.size()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}
}

void refreshPersistedItem(ClipboardBoardModel *boardModel,
                          ClipboardBoardService *boardService,
                          int row) {
    if (!boardModel || !boardService || row < 0) {
        return;
    }

    const ClipboardItem existing = boardModel->itemAt(row);
    if (existing.getName().isEmpty()) {
        return;
    }

    ClipboardItem reloaded = boardService->loadItemLight(boardService->filePathForItem(existing));
    if (reloaded.getName().isEmpty()) {
        return;
    }

    boardModel->updateItem(row, reloaded);
}

bool persistItemMetadata(ClipboardBoardService *boardService,
                         ClipboardBoardModel *boardModel,
                         int row,
                         const ClipboardItem &item) {
    if (!boardService || item.getName().isEmpty()) {
        return false;
    }

    const QString filePath = boardService->filePathForItem(item);
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        return false;
    }

    LocalSaver saver;
    if (saver.updateMetadata(filePath, item.getAlias(), item.isPinned())) {
        boardService->refreshIndexedItemForPath(filePath);
        refreshPersistedItem(boardModel, boardService, row);
        return true;
    }

    ClipboardItem fullItem = saver.loadFromFile(filePath);
    if (fullItem.getName().isEmpty()) {
        return false;
    }

    fullItem.setAlias(item.getAlias());
    fullItem.setPinned(item.isPinned());
    if (!saver.saveToFile(fullItem, filePath)) {
        return false;
    }
    boardService->refreshIndexedItemForPath(filePath);
    refreshPersistedItem(boardModel, boardService, row);
    return true;
}

QList<int> resolveRowsForItems(ClipboardBoardModel *boardModel,
                               const QList<ClipboardItem> &items) {
    QList<int> rows;
    if (!boardModel || items.isEmpty()) {
        return rows;
    }

    for (const ClipboardItem &item : items) {
        int row = boardModel->rowForMatchingItem(item);
        if (row < 0) {
            row = boardModel->rowForFingerprint(item.fingerprint());
        }
        if (row >= 0 && !rows.contains(row)) {
            rows.append(row);
        }
    }
    return rows;
}

int removeItems(ClipboardBoardService *boardService,
                ClipboardBoardModel *boardModel,
                const QList<ClipboardItem> &items) {
    if (!boardModel || items.isEmpty()) {
        return 0;
    }

    QList<int> rowsToRemove = resolveRowsForItems(boardModel, items);
    if (rowsToRemove.isEmpty()) {
        return 0;
    }

    std::sort(rowsToRemove.begin(), rowsToRemove.end(), std::greater<int>());
    for (const int row : rowsToRemove) {
        if (boardService) {
            // Use quiet delete: the model row is removed directly below,
            // no need for localPersistenceChanged → full page reload.
            boardService->deleteItemByPathQuiet(boardService->filePathForItem(boardModel->itemAt(row)));
        }
        boardModel->removeItemAt(row);
    }
    return rowsToRemove.size();
}

bool removeItem(ClipboardBoardService *boardService,
                ClipboardBoardModel *boardModel,
                const ClipboardItem &item) {
    if (!boardModel || item.getName().isEmpty()) {
        return false;
    }

    const int removed = removeItems(boardService, boardModel, {item});
    if (removed > 0) {
        return true;
    }

    if (boardService) {
        const QString filePath = boardService->filePathForItem(item);
        return boardService->deletePendingItemByPath(filePath);
    }
    return false;
}

bool setFavorite(ClipboardBoardModel *boardModel,
                 const ClipboardItem &item,
                 bool favorite) {
    if (!boardModel || item.getName().isEmpty()) {
        return false;
    }

    int row = boardModel->rowForMatchingItem(item);
    if (row < 0) {
        row = boardModel->rowForFingerprint(item.fingerprint());
    }
    if (row < 0 || boardModel->isFavorite(row) == favorite) {
        return false;
    }

    boardModel->setFavoriteByFingerprint(item.fingerprint(), favorite);
    return true;
}

bool applyPinnedState(ClipboardBoardModel *boardModel,
                      ClipboardBoardService *boardService,
                      int row,
                      int targetRow,
                      bool pinned) {
    if (!boardModel || row < 0) {
        return false;
    }

    ClipboardItem updated = boardModel->itemAt(row);
    if (updated.getName().isEmpty() || updated.isPinned() == pinned) {
        return false;
    }

    updated.setPinned(pinned);
    boardModel->updateItem(row, updated);
    boardModel->moveItemToRow(row, targetRow);
    if (boardService) {
        persistItemMetadata(boardService, boardModel, targetRow, updated);
    }
    return true;
}

bool exportItemToFile(const ClipboardItem &item,
                      const QString &filePath,
                      QString *errorMessage) {
    if (filePath.isEmpty()) {
        return false;
    }

    switch (item.getContentType()) {
        case Text:
            return writeUtf8File(filePath, item.getNormalizedText(), errorMessage);
        case RichText: {
            const QMimeData *mimeData = item.getMimeData();
            const QString html = mimeData ? mimeData->html() : QString();
            if (!mimeData || !mimeData->hasHtml()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("The current item does not contain savable HTML content.");
                }
                return false;
            }
            return writeUtf8File(filePath, html, errorMessage);
        }
        case Image: {
            item.getMimeData();
            const QPixmap pixmap = item.getImage();
            if (!pixmap.isNull() && pixmap.save(filePath)) {
                return true;
            }
            if (errorMessage && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("Unable to write the image to the target file.");
            }
            return false;
        }
        default:
            if (errorMessage) {
                *errorMessage = QStringLiteral("The current item does not support export.");
            }
            return false;
    }
}

} // namespace ClipboardBoardActionService
