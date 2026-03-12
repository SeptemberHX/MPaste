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

void ClipboardBoardProxyModel::setTypeFilter(ClipboardItem::ContentType type) {
    if (typeFilter_ == type) {
        return;
    }

    typeFilter_ = type;
    invalidateFilter();
}

ClipboardItem::ContentType ClipboardBoardProxyModel::typeFilter() const {
    return typeFilter_;
}

void ClipboardBoardProxyModel::setAsyncMatchedNames(const QSet<QString> &names) {
    asyncMatchedNames_ = names;
    invalidateFilter();
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

    if (typeFilter_ != ClipboardItem::All && item.getContentType() != typeFilter_) {
        return false;
    }

    if (keyword_.isEmpty()) {
        return true;
    }

    return item.contains(keyword_) || asyncMatchedNames_.contains(item.getName());
}
