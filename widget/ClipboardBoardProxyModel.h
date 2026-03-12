// input: Depends on Qt proxy-model APIs and ClipboardBoardModel item roles.
// output: Exposes keyword/type filtering for delegate-based clipboard boards.
// pos: Widget-layer proxy model used by ScrollItemsWidget filtering.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDBOARDPROXYMODEL_H
#define MPASTE_CLIPBOARDBOARDPROXYMODEL_H

#include <QSet>
#include <QSortFilterProxyModel>

#include "data/ClipboardItem.h"

class ClipboardBoardProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit ClipboardBoardProxyModel(QObject *parent = nullptr);

    void setKeyword(const QString &keyword);
    QString keyword() const;

    void setTypeFilter(ClipboardItem::ContentType type);
    ClipboardItem::ContentType typeFilter() const;

    void setAsyncMatchedNames(const QSet<QString> &names);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString keyword_;
    ClipboardItem::ContentType typeFilter_ = ClipboardItem::All;
    QSet<QString> asyncMatchedNames_;
};

#endif // MPASTE_CLIPBOARDBOARDPROXYMODEL_H
