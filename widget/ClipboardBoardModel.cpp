// input: Depends on ClipboardBoardModel.h and ClipboardItem metatype support.
// output: Implements the in-memory clipboard board row model with alias-aware metadata for delegate-based views.
// pos: Widget-layer model implementation used by ScrollItemsWidget.
// update: If I change, update this header block and my folder README.md (metadata updates for link previews + preview kind + row index for thumbnail refreshes).
#include "ClipboardBoardModel.h"

namespace {

QList<int> changedRolesForItem(const ClipboardItem &existing, const ClipboardItem &item) {
    QList<int> roles;
    roles.reserve(16);

    // Skip expensive operator== for ItemRole — the per-field checks
    // below cover all roles the delegate actually uses.
    roles.append(ClipboardBoardModel::ItemRole);
    if (existing.getNormalizedText() != item.getNormalizedText()) {
        roles.append(Qt::DisplayRole);
        roles.append(ClipboardBoardModel::NormalizedTextRole);
    }
    if (existing.getContentType() != item.getContentType()) {
        roles.append(ClipboardBoardModel::ContentTypeRole);
    }
    if (existing.getPreviewKind() != item.getPreviewKind()) {
        roles.append(ClipboardBoardModel::PreviewKindRole);
    }
    if (existing.getIcon().cacheKey() != item.getIcon().cacheKey()) {
        roles.append(ClipboardBoardModel::IconRole);
    }
    if (existing.thumbnail().cacheKey() != item.thumbnail().cacheKey()) {
        roles.append(ClipboardBoardModel::ThumbnailRole);
    }
    if (existing.getFavicon().cacheKey() != item.getFavicon().cacheKey()) {
        roles.append(ClipboardBoardModel::FaviconRole);
    }
    if (existing.getTitle() != item.getTitle()) {
        roles.append(ClipboardBoardModel::TitleRole);
    }
    if (existing.getUrl() != item.getUrl()) {
        roles.append(ClipboardBoardModel::UrlRole);
    }
    if (existing.getAlias() != item.getAlias()) {
        roles.append(ClipboardBoardModel::AliasRole);
    }
    if (existing.isPinned() != item.isPinned()) {
        roles.append(ClipboardBoardModel::PinnedRole);
    }
    if (existing.getNormalizedUrls() != item.getNormalizedUrls()) {
        roles.append(ClipboardBoardModel::NormalizedUrlsRole);
    }
    if (existing.getTime() != item.getTime()) {
        roles.append(ClipboardBoardModel::TimeRole);
    }
    if (existing.getImagePixelSize() != item.getImagePixelSize()) {
        roles.append(ClipboardBoardModel::ImageSizeRole);
    }
    if (existing.getColor() != item.getColor()) {
        roles.append(ClipboardBoardModel::ColorRole);
    }
    if (existing.getName() != item.getName()) {
        roles.append(ClipboardBoardModel::NameRole);
    }

    if (roles.isEmpty()) {
        roles.append(ClipboardBoardModel::ItemRole);
    }
    return roles;
}

} // namespace

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
            return {};
        case ItemRole:
            return QVariant::fromValue(entry.item);
        case FavoriteRole:
            return entry.favorite;
        case ShortcutTextRole:
            return entry.shortcutText;
        case ContentTypeRole:
            return static_cast<int>(entry.item.getContentType());
        case PreviewKindRole:
            return static_cast<int>(entry.item.getPreviewKind());
        case PreviewStateRole:
            return static_cast<int>(entry.previewState);
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
        case AliasRole:
            return entry.item.getAlias();
        case PinnedRole:
            return entry.item.isPinned();
        case NormalizedTextRole: {
            // Return only enough chars for the card preview area (~3-5
            // lines).  Full text is read on demand for paste/preview.
            const QString &text = entry.item.getNormalizedText();
            return text.size() > 200 ? text.left(200) : text;
        }
        case TextLengthRole:
            return entry.item.getNormalizedText().size();
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
    switch (role) {
        case ItemRole: {
            const ClipboardItem item = value.value<ClipboardItem>();
            const QList<int> roles = changedRolesForItem(entry.item, item);
            if (entry.item == item && roles.size() == 1 && roles.first() == ItemRole) {
                return false;
            }

            const QString previousName = entry.item.getName();
            entry.item = item;
            if (previousName != item.getName()) {
                rebuildNameRowIndex();
            } else if (!item.getName().isEmpty()) {
                nameRowIndex_.insert(item.getName(), index.row());
            }
            emit dataChanged(index, index, roles);
            return true;
        }
        case FavoriteRole: {
            const bool favorite = value.toBool();
            if (entry.favorite == favorite) {
                return false;
            }
            entry.favorite = favorite;
            emit dataChanged(index, index, {FavoriteRole});
            return true;
        }
        case ShortcutTextRole: {
            const QString shortcutText = value.toString();
            if (entry.shortcutText == shortcutText) {
                return false;
            }
            entry.shortcutText = shortcutText;
            emit dataChanged(index, index, {ShortcutTextRole});
            return true;
        }
        default:
            return false;
    }
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
    nameRowIndex_.clear();
    endResetModel();
}

int ClipboardBoardModel::prependItem(const ClipboardItem &item, bool favorite) {
    beginInsertRows(QModelIndex(), 0, 0);
    entries_.prepend({item, favorite, QString(), PreviewNotApplicable});
    endInsertRows();
    rebuildNameRowIndex();
    return 0;
}

int ClipboardBoardModel::appendItem(const ClipboardItem &item, bool favorite) {
    const int row = entries_.size();
    beginInsertRows(QModelIndex(), row, row);
    entries_.append({item, favorite, QString(), PreviewNotApplicable});
    endInsertRows();
    if (!item.getName().isEmpty()) {
        nameRowIndex_.insert(item.getName(), row);
    }
    return row;
}

int ClipboardBoardModel::insertItem(int row, const ClipboardItem &item, bool favorite) {
    const int clampedRow = qBound(0, row, entries_.size());
    beginInsertRows(QModelIndex(), clampedRow, clampedRow);
    entries_.insert(clampedRow, {item, favorite, QString(), PreviewNotApplicable});
    endInsertRows();
    rebuildNameRowIndex();
    return clampedRow;
}

bool ClipboardBoardModel::updateItem(int row, const ClipboardItem &item) {
    if (row < 0 || row >= entries_.size()) {
        return false;
    }

    const ClipboardItem &existing = entries_[row].item;
    const QList<int> roles = changedRolesForItem(existing, item);
    // If only ItemRole changed (always included), nothing visible differs.
    if (roles.size() == 1 && roles.first() == ItemRole) {
        return false;
    }

    const QString previousName = existing.getName();
    entries_[row].item = item;
    if (previousName != item.getName()) {
        rebuildNameRowIndex();
    } else if (!item.getName().isEmpty()) {
        nameRowIndex_.insert(item.getName(), row);
    }
    const QModelIndex modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex, roles);
    return true;
}

bool ClipboardBoardModel::removeItemAt(int row) {
    if (row < 0 || row >= entries_.size()) {
        return false;
    }

    beginRemoveRows(QModelIndex(), row, row);
    entries_.removeAt(row);
    endRemoveRows();
    rebuildNameRowIndex();
    return true;
}

bool ClipboardBoardModel::moveItemToFront(int row) {
    if (row <= 0 || row >= entries_.size()) {
        return row == 0;
    }

    beginMoveRows(QModelIndex(), row, row, QModelIndex(), 0);
    entries_.move(row, 0);
    endMoveRows();
    rebuildNameRowIndex();
    return true;
}

bool ClipboardBoardModel::moveItemToRow(int row, int targetRow) {
    if (row < 0 || row >= entries_.size()) {
        return false;
    }
    const int clampedTarget = qBound(0, targetRow, entries_.size() - 1);
    if (row == clampedTarget) {
        return true;
    }
    const int destination = row < clampedTarget ? clampedTarget + 1 : clampedTarget;
    beginMoveRows(QModelIndex(), row, row, QModelIndex(), destination);
    entries_.move(row, clampedTarget);
    endMoveRows();
    rebuildNameRowIndex();
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
    return nameRowIndex_.value(name, -1);
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

void ClipboardBoardModel::releaseItemPixmaps(int row) {
    if (row < 0 || row >= entries_.size()) {
        return;
    }
    ClipboardItem &item = entries_[row].item;
    // Only release the thumbnail (the large preview image).
    // Keep icon and favicon — they are small and needed when the
    // card cache is invalidated (theme/scale change).
    item.setThumbnail(QPixmap());
    // No dataChanged — the card is already cached in cardPixmapCache_.
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

bool ClipboardBoardModel::setPreviewState(int row, PreviewState state) {
    if (row < 0 || row >= entries_.size()) {
        return false;
    }
    Entry &entry = entries_[row];
    if (entry.previewState == state) {
        return false;
    }
    entry.previewState = state;
    const QModelIndex modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex, {PreviewStateRole});
    return true;
}

ClipboardBoardModel::PreviewState ClipboardBoardModel::previewState(int row) const {
    if (row < 0 || row >= entries_.size()) {
        return PreviewNotApplicable;
    }
    return entries_.at(row).previewState;
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

void ClipboardBoardModel::rebuildNameRowIndex() {
    nameRowIndex_.clear();
    nameRowIndex_.reserve(entries_.size());
    for (int row = 0; row < entries_.size(); ++row) {
        const QString &name = entries_.at(row).item.getName();
        if (!name.isEmpty()) {
            nameRowIndex_.insert(name, row);
        }
    }
}
