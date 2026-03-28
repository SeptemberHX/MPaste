// input: Depends on ClipboardBoardProxyModel.h and board model item-role payloads.
// output: Implements keyword and content-type filtering for clipboard boards.
// pos: Widget-layer proxy implementation that keeps ScrollItemsWidget filtering lightweight.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardBoardProxyModel.h"

#include "ClipboardBoardModel.h"

ClipboardBoardProxyModel::ClipboardBoardProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {}

void ClipboardBoardProxyModel::setKeyword(const QString &keyword) {
    if (keyword_ == keyword) {
        return;
    }

    keyword_ = keyword;
    invalidateFilter();
}

QString ClipboardBoardProxyModel::keyword() const {
    return keyword_;
}

void ClipboardBoardProxyModel::setTypeFilter(ContentType type) {
    if (typeFilter_ == type) {
        return;
    }

    typeFilter_ = type;
    invalidateFilter();
}

ContentType ClipboardBoardProxyModel::typeFilter() const {
    return typeFilter_;
}

void ClipboardBoardProxyModel::setAsyncMatchedNames(const QSet<QString> &names) {
    if (asyncMatchedNames_ == names) {
        return;
    }
    asyncMatchedNames_ = names;
    invalidateFilter();
}

void ClipboardBoardProxyModel::setPageSize(int pageSize) {
    const int clamped = qMax(0, pageSize);
    if (pageSize_ == clamped) {
        return;
    }
    pageSize_ = clamped;
    invalidateFilter();
}

int ClipboardBoardProxyModel::pageSize() const {
    return pageSize_;
}

void ClipboardBoardProxyModel::setPageIndex(int pageIndex) {
    const int clamped = qMax(0, pageIndex);
    if (pageIndex_ == clamped) {
        return;
    }
    pageIndex_ = clamped;
    invalidateFilter();
}

int ClipboardBoardProxyModel::pageIndex() const {
    return pageIndex_;
}

bool ClipboardBoardProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    if (!sourceModel()) {
        return false;
    }

    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    const ClipboardItem item = sourceIndex.data(ClipboardBoardModel::ItemRole).value<ClipboardItem>();
    if (item.getName().isEmpty()) {
        return false;
    }

    if (typeFilter_ != All && item.getContentType() != typeFilter_) {
        return false;
    }

    const bool keywordAccepted = keyword_.isEmpty()
        || item.contains(keyword_)
        || asyncMatchedNames_.contains(item.getName());
    if (!keywordAccepted) {
        return false;
    }

    if (pageSize_ <= 0) {
        return true;
    }

    const qint64 startRow = static_cast<qint64>(pageIndex_) * static_cast<qint64>(pageSize_);
    const qint64 endRow = startRow + static_cast<qint64>(pageSize_);
    return sourceRow >= startRow && static_cast<qint64>(sourceRow) < endRow;
}
