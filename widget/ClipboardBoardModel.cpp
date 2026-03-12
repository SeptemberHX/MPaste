// input: Depends on ClipboardBoardModel.h and ClipboardItem metatype support.
// output: Implements the in-memory clipboard board row model for delegate-based views.
// pos: Widget-layer model implementation used by ScrollItemsWidget.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardBoardModel.h"

ClipboardBoardModel::ClipboardBoardModel(QObject *parent)
    : QAbstractListModel(parent) {}

int ClipboardBoardModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : entries_.size();
}

QVariant ClipboardBoardModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) {
        return {};
    }

    const Entry &entry = entries_.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
            return entry.item.getNormalizedText();
        case Qt::ToolTipRole:
            return entry.item.getNormalizedText();
        case ItemRole:
            return QVariant::fromValue(entry.item);
        case FavoriteRole:
            return entry.favorite;
        case ShortcutTextRole:
            return entry.shortcutText;
        case ContentTypeRole:
            return static_cast<int>(entry.item.getContentType());
        case IconRole:
            return entry.item.getIcon();
        case ThumbnailRole:
            return entry.item.thumbnail();
        case FaviconRole:
            return entry.item.getFavicon();
        case TitleRole:
            return entry.item.getTitle();
        case UrlRole:
            return entry.item.getUrl();
        case NormalizedTextRole:
            return entry.item.getNormalizedText();
        case NormalizedUrlsRole:
            return QVariant::fromValue(entry.item.getNormalizedUrls());
        case TimeRole:
            return entry.item.getTime();
        case ImageSizeRole:
            return entry.item.getImagePixelSize();
        case ColorRole:
            return entry.item.getColor();
        case NameRole:
            return entry.item.getName();
        default:
            return {};
    }
}

bool ClipboardBoardModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) {
        return false;
    }

    Entry &entry = entries_[index.row()];
    bool changed = false;
    switch (role) {
        case ItemRole: {
            const ClipboardItem item = value.value<ClipboardItem>();
            if (!(entry.item == item)) {
                entry.item = item;
                changed = true;
            }
            break;
        }
        case FavoriteRole: {
            const bool favorite = value.toBool();
            if (entry.favorite != favorite) {
                entry.favorite = favorite;
                changed = true;
            }
            break;
        }
        case ShortcutTextRole: {
            const QString shortcutText = value.toString();
            if (entry.shortcutText != shortcutText) {
                entry.shortcutText = shortcutText;
                changed = true;
            }
            break;
        }
        default:
            break;
    }

    if (!changed) {
        return false;
    }

    emit dataChanged(index, index, {});
    return true;
}

Qt::ItemFlags ClipboardBoardModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void ClipboardBoardModel::clear() {
    if (entries_.isEmpty()) {
        return;
    }

    beginResetModel();
    entries_.clear();
    endResetModel();
}

int ClipboardBoardModel::prependItem(const ClipboardItem &item, bool favorite) {
    beginInsertRows(QModelIndex(), 0, 0);
    entries_.prepend({item, favorite, QString()});
    endInsertRows();
    return 0;
}

int ClipboardBoardModel::appendItem(const ClipboardItem &item, bool favorite) {
    const int row = entries_.size();
    beginInsertRows(QModelIndex(), row, row);
    entries_.append({item, favorite, QString()});
    endInsertRows();
    return row;
}

bool ClipboardBoardModel::updateItem(int row, const ClipboardItem &item) {
    if (row < 0 || row >= entries_.size()) {
        return false;
    }

    if (entries_[row].item == item) {
        return false;
    }

    entries_[row].item = item;
    const QModelIndex modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex, {});
    return true;
}

bool ClipboardBoardModel::removeItemAt(int row) {
    if (row < 0 || row >= entries_.size()) {
        return false;
    }

    beginRemoveRows(QModelIndex(), row, row);
    entries_.removeAt(row);
    endRemoveRows();
    return true;
}

bool ClipboardBoardModel::moveItemToFront(int row) {
    if (row <= 0 || row >= entries_.size()) {
        return row == 0;
    }

    beginMoveRows(QModelIndex(), row, row, QModelIndex(), 0);
    entries_.move(row, 0);
    endMoveRows();
    return true;
}

int ClipboardBoardModel::rowForMatchingItem(const ClipboardItem &item) const {
    const QByteArray fingerprint = item.fingerprint();
    for (int row = 0; row < entries_.size(); ++row) {
        const Entry &entry = entries_.at(row);
        if (entry.item.fingerprint() == fingerprint && entry.item == item) {
            return row;
        }
    }
    return -1;
}

int ClipboardBoardModel::rowForName(const QString &name) const {
    if (name.isEmpty()) {
        return -1;
    }

    for (int row = 0; row < entries_.size(); ++row) {
        if (entries_.at(row).item.getName() == name) {
            return row;
        }
    }
    return -1;
}

int ClipboardBoardModel::rowForFingerprint(const QByteArray &fingerprint) const {
    if (fingerprint.isEmpty()) {
        return -1;
    }

    for (int row = 0; row < entries_.size(); ++row) {
        if (entries_.at(row).item.fingerprint() == fingerprint) {
            return row;
        }
    }
    return -1;
}

ClipboardItem ClipboardBoardModel::itemAt(int row) const {
    if (row < 0 || row >= entries_.size()) {
        return {};
    }
    return entries_.at(row).item;
}

const ClipboardItem *ClipboardBoardModel::itemPtrAt(int row) const {
    if (row < 0 || row >= entries_.size()) {
        return nullptr;
    }
    return &entries_.at(row).item;
}

QList<ClipboardItem> ClipboardBoardModel::items() const {
    QList<ClipboardItem> result;
    result.reserve(entries_.size());
    for (const Entry &entry : entries_) {
        result.append(entry.item);
    }
    return result;
}

bool ClipboardBoardModel::isFavorite(int row) const {
    return row >= 0 && row < entries_.size() ? entries_.at(row).favorite : false;
}

void ClipboardBoardModel::setFavoriteByFingerprint(const QByteArray &fingerprint, bool favorite) {
    if (fingerprint.isEmpty()) {
        return;
    }

    for (int row = 0; row < entries_.size(); ++row) {
        Entry &entry = entries_[row];
        if (entry.item.fingerprint() != fingerprint || entry.favorite == favorite) {
            continue;
        }

        entry.favorite = favorite;
        const QModelIndex modelIndex = index(row, 0);
        emit dataChanged(modelIndex, modelIndex, {FavoriteRole});
    }
}

void ClipboardBoardModel::clearShortcutTexts() {
    if (entries_.isEmpty()) {
        return;
    }

    for (int row = 0; row < entries_.size(); ++row) {
        if (entries_[row].shortcutText.isEmpty()) {
            continue;
        }

        entries_[row].shortcutText.clear();
        const QModelIndex modelIndex = index(row, 0);
        emit dataChanged(modelIndex, modelIndex, {ShortcutTextRole});
    }
}

void ClipboardBoardModel::setShortcutText(int row, const QString &shortcutText) {
    if (row < 0 || row >= entries_.size()) {
        return;
    }

    if (entries_[row].shortcutText == shortcutText) {
        return;
    }

    entries_[row].shortcutText = shortcutText;
    const QModelIndex modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex, {ShortcutTextRole});
}
