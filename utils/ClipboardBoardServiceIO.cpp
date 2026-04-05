// input: Depends on ClipboardBoardService.h, LocalSaver, and Qt IO utilities.
// output: Implements board persistence: save, delete, and file-path operations.
// pos: utils layer board service implementation (IO / persistence).
// update: If I change, update this header block and my folder README.md.
#include "ClipboardBoardService.h"
#include "ClipboardBoardServiceInternal.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QTimer>
#include <QFileInfo>

#include "data/LocalSaver.h"
#include "utils/MPasteSettings.h"
#include "utils/ThumbnailBuilder.h"

// --- Persistence ---

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
