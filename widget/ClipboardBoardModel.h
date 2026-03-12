// input: Depends on Qt item-model APIs and ClipboardItem value semantics.
// output: Exposes a lightweight board model that stores clipboard rows for delegate painting.
// pos: Widget-layer model backing clipboard and favorites boards.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDBOARDMODEL_H
#define MPASTE_CLIPBOARDBOARDMODEL_H

#include <QAbstractListModel>
#include <QVector>

#include "data/ClipboardItem.h"

class ClipboardBoardModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        ItemRole = Qt::UserRole + 1,
        FavoriteRole,
        ShortcutTextRole,
        ContentTypeRole,
        IconRole,
        ThumbnailRole,
        FaviconRole,
        TitleRole,
        UrlRole,
        NormalizedTextRole,
        NormalizedUrlsRole,
        TimeRole,
        ImageSizeRole,
        ColorRole,
        NameRole
    };

    struct Entry {
        ClipboardItem item;
        bool favorite = false;
        QString shortcutText;
    };

    explicit ClipboardBoardModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void clear();
    int prependItem(const ClipboardItem &item, bool favorite);
    int appendItem(const ClipboardItem &item, bool favorite);
    bool updateItem(int row, const ClipboardItem &item);
    bool removeItemAt(int row);
    bool moveItemToFront(int row);

    int rowForMatchingItem(const ClipboardItem &item) const;
    int rowForName(const QString &name) const;
    int rowForFingerprint(const QByteArray &fingerprint) const;

    ClipboardItem itemAt(int row) const;
    const ClipboardItem *itemPtrAt(int row) const;
    QList<ClipboardItem> items() const;

    bool isFavorite(int row) const;
    void setFavoriteByFingerprint(const QByteArray &fingerprint, bool favorite);
    void clearShortcutTexts();
    void setShortcutText(int row, const QString &shortcutText);

private:
    QVector<Entry> entries_;
};

#endif // MPASTE_CLIPBOARDBOARDMODEL_H
