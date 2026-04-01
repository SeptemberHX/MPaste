// input: Depends on ScrollItemsWidget.h, ClipboardBoardService, Qt model/view APIs, and delegate-based card painting.
// output: Implements lazy-loaded boards, proxy filtering, async thumbnail completion, and list-view item interaction.
// pos: Widget-layer board implementation driving clipboard and favorites history lists.
// update: If I change, update this header block and my folder README.md (arrow navigation no longer forces center + hover action bar + main-card save/export + multi-select batch actions + data-layer preview kind + board paint FPS logging + hidden-stage light prewarm).
// note: Context menu methods in BoardContextMenu.cpp, hover action bar in BoardHoverActions.cpp, pagination in BoardPagination.cpp, widget classes in BoardViewWidgets.h.
#include <QDir>
#include <QFile>

#include "data/LocalSaver.h"
#include <QFileInfo>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScroller>
#include <QShowEvent>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QDateTime>

#include <utils/MPasteSettings.h>

#include "BoardInternalHelpers.h"
#include "BoardViewWidgets.h"
#include "ClipboardBoardModel.h"
#include "ClipboardBoardProxyModel.h"
#include "ClipboardBoardActionService.h"
#include "ClipboardCardDelegate.h"
#include "ClipboardItemRenameDialog.h"
#include "CardMetrics.h"
#include "ScrollItemsWidget.h"
#include "ui_ScrollItemsWidget.h"
#include "utils/OpenGraphFetcher.h"
#include "utils/ThemeManager.h"
#include "utils/IconResolver.h"

using namespace BoardHelpers;

ScrollItemsWidget::ScrollItemsWidget(const QString &category, const QString &borderColor, QWidget *parent)
    : QWidget(parent),
      ui(new Ui::ScrollItemsWidget),
      category(category),
      borderColor(borderColor),
      boardService_(new ClipboardBoardService(category, this)),
      scrollAnimation(nullptr) {
    ui->setupUi(this);

    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize itemOuterSize = cardOuterSizeForScale(scale);

    auto *hostLayout = new QVBoxLayout(ui->viewHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    auto *boardView = new ClipboardBoardView(category, ui->viewHost);
    listView_ = boardView;
    listView_->setObjectName(QStringLiteral("boardListView"));
    hostLayout->addWidget(listView_);

    boardModel_ = new ClipboardBoardModel(this);
    filterProxyModel_ = new ClipboardBoardProxyModel(this);
    filterProxyModel_->setSourceModel(boardModel_);
    proxyModel_ = new ClipboardBoardProxyModel(this);
    proxyModel_->setSourceModel(filterProxyModel_);
    proxyModel_->setPageSize(PAGE_SIZE);
    cardDelegate_ = new ClipboardCardDelegate(QColor::fromString(this->borderColor), listView_);

    listView_->setModel(proxyModel_);
    listView_->setItemDelegate(cardDelegate_);

    // Invalidate card cache when model data changes (pin, favorite, etc.)
    connect(boardModel_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
        if (!cardDelegate_) return;
        for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
            const QString name = boardModel_->data(boardModel_->index(row, 0),
                                                   ClipboardBoardModel::NameRole).toString();
            if (!name.isEmpty()) {
                cardDelegate_->invalidateCard(name);
            }
        }
    });
    listView_->setViewMode(QListView::IconMode);
    listView_->setFlow(QListView::LeftToRight);
    listView_->setWrapping(false);
    listView_->setMovement(QListView::Static);
    listView_->setResizeMode(QListView::Adjust);
    listView_->setLayoutMode(QListView::SinglePass);
    listView_->setUniformItemSizes(true);
    listView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listView_->setSelectionBehavior(QAbstractItemView::SelectItems);
    listView_->setSelectionRectVisible(false);
    listView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    listView_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    listView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    listView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    listView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listView_->setFrameShape(QFrame::NoFrame);
    listView_->setContextMenuPolicy(Qt::CustomContextMenu);
    listView_->setMouseTracking(true);
    listView_->setSpacing(qMax(6, 8 * scale / 100));
    listView_->setGridSize(QSize(itemOuterSize.width() + listView_->spacing(), itemOuterSize.height()));
    listView_->setStyleSheet(QStringLiteral("QListView { background: transparent; border: none; }"));
    listView_->setAttribute(Qt::WA_TranslucentBackground);
    listView_->viewport()->setAttribute(Qt::WA_TranslucentBackground);

    loadingLabel_ = new QLabel(ui->viewHost);
    loadingLabel_->setObjectName(QStringLiteral("loadingOverlay"));
    loadingLabel_->setAlignment(Qt::AlignCenter);
    loadingLabel_->setWordWrap(true);
    loadingLabel_->setText(QStringLiteral("Loading...\n正在加载..."));
    loadingLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    loadingLabel_->hide();

    const int scrollbarHeight = qMax(12, listView_->horizontalScrollBar()->sizeHint().height());
    const int scrollHeight = itemOuterSize.height() + scrollbarHeight;
    edgeContentPadding_ = qMax(6, 8 * scale / 100);
    edgeFadeWidth_ = qMax(12, 16 * scale / 100);
    boardView->applyViewportMargins(edgeContentPadding_, 0, edgeContentPadding_, 0);
    listView_->setFixedHeight(scrollHeight);
    ui->viewHost->setFixedHeight(scrollHeight);
    setFixedHeight(scrollHeight);

    QScroller::grabGesture(listView_->viewport(), QScroller::TouchGesture);
    QScroller *scroller = QScroller::scroller(listView_->viewport());
    QScrollerProperties sp = scroller->scrollerProperties();
    sp.setScrollMetric(QScrollerProperties::FrameRate, QVariant::fromValue(QScrollerProperties::Fps60));
    sp.setScrollMetric(QScrollerProperties::DragStartDistance, 0.0025);
    sp.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                       QVariant::fromValue(QScrollerProperties::OvershootAlwaysOn));
    sp.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                       QVariant::fromValue(QScrollerProperties::OvershootAlwaysOn));
    scroller->setScrollerProperties(sp);

    listView_->horizontalScrollBar()->setSingleStep(48);
    scrollAnimation = new QPropertyAnimation(listView_->horizontalScrollBar(), "value", this);
    scrollAnimation->setEasingCurve(QEasingCurve::OutQuad);
    scrollAnimation->setDuration(70);

    connect(boardService_, &ClipboardBoardService::itemsLoaded, this, &ScrollItemsWidget::handleLoadedItems);
    connect(boardService_, &ClipboardBoardService::pendingItemReady, this, &ScrollItemsWidget::handlePendingItemReady);
    connect(boardService_, &ClipboardBoardService::thumbnailReady, this, &ScrollItemsWidget::handleThumbnailReady);
    connect(boardService_, &ClipboardBoardService::keywordMatched, this, &ScrollItemsWidget::handleKeywordMatched);
    connect(boardService_, &ClipboardBoardService::totalItemCountChanged, this, &ScrollItemsWidget::handleTotalItemCountChanged);
    connect(boardService_, &ClipboardBoardService::deferredLoadCompleted, this, &ScrollItemsWidget::handleDeferredLoadCompleted);
    connect(boardService_, &ClipboardBoardService::localPersistenceChanged, this, [this]() {
        emit localPersistenceChanged();
        if (!paginationEnabled()) {
            return;
        }
        loadedPage_ = -1;
        loadedPageBaseOffset_ = -1;
        loadedPageTotalItems_ = -1;
        if (boardService_) {
            syncPageWindow(false);
        }
        if (isBoardUiVisible()) {
            refreshContentWidthHint();
            primeVisibleThumbnailsSync();
            scheduleThumbnailUpdate();
        }
    });

    connect(listView_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &ScrollItemsWidget::handleCurrentIndexChanged);
    connect(listView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &, const QItemSelection &) {
                updateSelectionState();
            });
    connect(listView_, &QListView::doubleClicked, this, &ScrollItemsWidget::handleActivatedIndex);
    connect(listView_, &QListView::customContextMenuRequested, this, &ScrollItemsWidget::showContextMenu);
    connect(listView_->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) {
                maybeLoadMoreItems();
                scheduleThumbnailUpdate();
            });

    leftEdgeFadeOverlay_ = new EdgeFadeOverlay(EdgeFadeOverlay::Left, ui->viewHost);
    rightEdgeFadeOverlay_ = new EdgeFadeOverlay(EdgeFadeOverlay::Right, ui->viewHost);
    updateEdgeFadeOverlays();
    leftEdgeFadeOverlay_->show();
    rightEdgeFadeOverlay_->show();

    listView_->installEventFilter(this);
    listView_->viewport()->installEventFilter(this);

    createHoverActionBar();
    connect(listView_->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        updateHoverActionBarPosition();
    });

    thumbnailPulseTimer_ = new QTimer(this);
    thumbnailPulseTimer_->setInterval(220);
    connect(thumbnailPulseTimer_, &QTimer::timeout, this, [this]() {
        if (!cardDelegate_ || !listView_) {
            return;
        }
        thumbnailLoadingPhase_ = (thumbnailLoadingPhase_ + 7) % 100;
        cardDelegate_->setLoadingPhase(thumbnailLoadingPhase_);
        updateThumbnailViewport(visibleLoadingThumbnailNames_);
    });

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ScrollItemsWidget::applyTheme);
    updateLoadingOverlay();
}

ScrollItemsWidget::~ScrollItemsWidget() {
    delete ui;
}

void ScrollItemsWidget::applyTheme(bool dark) {
    darkTheme_ = dark;

    const int scale = MPasteSettings::getInst()->getItemScale();
    const int borderRadius = qMax(4, 6 * scale / 100);
    const int padding = qMax(2, 3 * scale / 100);
    const QString buttonStyle = hoverButtonStyle(darkTheme_, borderRadius, padding);

    if (hoverActionBar_) {
        auto *hoverBar = static_cast<HoverActionBar *>(hoverActionBar_);
        hoverBar->setColors(darkTheme_ ? QColor(26, 31, 38, 205) : QColor(255, 255, 255, 185),
                            darkTheme_ ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 120));
    }

    if (loadingLabel_) {
        const QString textColor = darkTheme_ ? QStringLiteral("#A7B2C4") : QStringLiteral("#6B7788");
        const QString glowColor = darkTheme_ ? QStringLiteral("rgba(31, 36, 45, 180)") : QStringLiteral("rgba(255, 255, 255, 180)");
        loadingLabel_->setStyleSheet(QStringLiteral("QLabel#loadingOverlay { color: %1; font-size: 16px; font-weight: 600; background: %2; border-radius: 12px; padding: 14px 18px; }")
                                         .arg(textColor, glowColor));
    }

    updateLoadingOverlay();

    if (hoverDetailsBtn_) {
        hoverDetailsBtn_->setIcon(IconResolver::themedIcon(QStringLiteral("details"), darkTheme_));
        hoverDetailsBtn_->setStyleSheet(buttonStyle);
    }
    if (hoverFavoriteBtn_) {
        hoverFavoriteBtn_->setStyleSheet(buttonStyle);
        bool favorite = false;
        if (hoverProxyIndex_.isValid() && proxyModel_ && boardModel_) {
            const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
            favorite = boardModel_->isFavorite(sourceRow);
        }
        updateHoverFavoriteButton(favorite);
    }
    if (hoverDeleteBtn_) {
        hoverDeleteBtn_->setStyleSheet(buttonStyle);
    }

    if (leftEdgeFadeOverlay_) {
        auto *overlay = static_cast<EdgeFadeOverlay *>(leftEdgeFadeOverlay_);
        overlay->setDark(darkTheme_);
    }
    if (rightEdgeFadeOverlay_) {
        auto *overlay = static_cast<EdgeFadeOverlay *>(rightEdgeFadeOverlay_);
        overlay->setDark(darkTheme_);
    }

    updateEdgeFadeOverlays();
    if (cardDelegate_) {
        cardDelegate_->clearVisualCaches();
    }
    if (listView_) {
        listView_->viewport()->update();
    }
}

QModelIndex ScrollItemsWidget::currentProxyIndex() const {
    return listView_ ? listView_->currentIndex() : QModelIndex();
}

QModelIndex ScrollItemsWidget::proxyIndexForSourceRow(int sourceRow) const {
    if (!proxyModel_ || !filterProxyModel_ || !boardModel_ || sourceRow < 0) {
        return {};
    }

    const QModelIndex filterIndex = filterProxyModel_->mapFromSource(boardModel_->index(sourceRow, 0));
    if (!filterIndex.isValid()) {
        return {};
    }
    return proxyModel_->mapFromSource(filterIndex);
}

int ScrollItemsWidget::sourceRowForProxyIndex(const QModelIndex &proxyIndex) const {
    if (!proxyModel_ || !filterProxyModel_ || !proxyIndex.isValid()) {
        return -1;
    }

    const QModelIndex filterIndex = proxyModel_->mapToSource(proxyIndex);
    if (!filterIndex.isValid()) {
        return -1;
    }

    return filterProxyModel_->mapToSource(filterIndex).row();
}

QList<QModelIndex> ScrollItemsWidget::selectedProxyIndexes() const {
    QList<QModelIndex> result;
    if (!listView_ || !listView_->selectionModel()) {
        return result;
    }

    // selectedRows() requires all columns to be selected; under SelectItems
    // mode this may return empty even when items are visually selected.
    // Fall back to selectedIndexes() filtered to column 0.
    result = listView_->selectionModel()->selectedRows();
    if (result.isEmpty()) {
        for (const QModelIndex &idx : listView_->selectionModel()->selectedIndexes()) {
            if (idx.column() == 0) {
                result.append(idx);
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const QModelIndex &lhs, const QModelIndex &rhs) {
        return lhs.row() < rhs.row();
    });

    if (!result.isEmpty()) {
        return result;
    }

    const QModelIndex current = currentProxyIndex();
    if (current.isValid()) {
        result.append(current);
    }
    return result;
}

QList<int> ScrollItemsWidget::selectedSourceRows() const {
    QList<int> result;
    if (!proxyModel_) {
        return result;
    }

    for (const QModelIndex &proxyIndex : selectedProxyIndexes()) {
        if (!proxyIndex.isValid()) {
            continue;
        }
        const int sourceRow = sourceRowForProxyIndex(proxyIndex);
        if (sourceRow >= 0 && !result.contains(sourceRow)) {
            result.append(sourceRow);
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

QList<ClipboardItem> ScrollItemsWidget::selectedItems() const {
    QList<ClipboardItem> result;
    if (!boardModel_) {
        return result;
    }

    for (const int sourceRow : selectedSourceRows()) {
        const ClipboardItem item = boardModel_->itemAt(sourceRow);
        if (!item.getName().isEmpty()) {
            result.append(item);
        }
    }
    return result;
}

void ScrollItemsWidget::setCurrentProxyIndex(const QModelIndex &index) {
    if (!listView_ || !listView_->selectionModel()) {
        return;
    }

    if (!index.isValid()) {
        listView_->selectionModel()->clearCurrentIndex();
        return;
    }

    listView_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
}

void ScrollItemsWidget::handleCurrentIndexChanged(const QModelIndex &current, const QModelIndex &previous) {
    Q_UNUSED(current);
    Q_UNUSED(previous);

    ensureLinkPreviewForIndex(current);
    if (hoverActionBar_ && hoverActionBar_->isVisible()) {
        updateHoverActionBar(current);
    }
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

void ScrollItemsWidget::ensureLinkPreviewForIndex(const QModelIndex &proxyIndex) {
    if (!proxyModel_ || !boardModel_ || !proxyIndex.isValid()) {
        return;
    }

    const int sourceRow = sourceRowForProxyIndex(proxyIndex);
    if (sourceRow < 0) {
        return;
    }

    ClipboardItem item = boardModel_->itemAt(sourceRow);
    if (item.getContentType() != Link) {
        return;
    }

    QString linkText = item.getUrl().trimmed();
    if (linkText.isEmpty()) {
        const QList<QUrl> urls = item.getNormalizedUrls();
        if (!urls.isEmpty()) {
            const QUrl &first = urls.first();
            linkText = first.isLocalFile() ? first.toLocalFile() : first.toString();
        }
    }
    if (linkText.isEmpty()) {
        linkText = item.getNormalizedText().left(512).trimmed();
    }
    if (linkText.isEmpty()) {
        return;
    }

    QUrl url(linkText);
    if (!url.isValid() || url.isRelative() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        return;
    }

    const QString requestKey = url.toString(QUrl::FullyEncoded);
    if (pendingLinkPreviewUrls_.contains(requestKey)) {
        return;
    }
    pendingLinkPreviewUrls_.insert(requestKey);

    const QByteArray fingerprint = item.fingerprint();
    auto *fetcher = new OpenGraphFetcher(url, this);
    connect(fetcher, &OpenGraphFetcher::finished, this, [this, fetcher, requestKey, fingerprint, linkText](const OpenGraphItem &ogItem) {
        pendingLinkPreviewUrls_.remove(requestKey);
        fetcher->deleteLater();

        if (ogItem.getImage().isNull() && ogItem.getTitle().isEmpty()) {
            return;
        }

        const int row = boardModel_->rowForFingerprint(fingerprint);
        if (row < 0) {
            return;
        }

        ClipboardItem updated = boardModel_->itemAt(row);

        // Icon/favicon may have been cleared by the memory cleanup cycle.
        // Reload from disk so the re-rendered card has the app icon.
        if (updated.getIcon().isNull() && boardService_) {
            const QString filePath = boardService_->filePathForName(updated.getName());
            if (!filePath.isEmpty()) {
                ClipboardItem diskItem = boardService_->loadItemLight(filePath, false);
                if (!diskItem.getIcon().isNull()) {
                    updated.setIcon(diskItem.getIcon());
                }
                if (updated.getFavicon().isNull() && !diskItem.getFavicon().isNull()) {
                    updated.setFavicon(diskItem.getFavicon());
                }
            }
        }

        if (!ogItem.getTitle().isEmpty()) {
            updated.setTitle(ogItem.getTitle());
        }
        if (!linkText.isEmpty()) {
            updated.setUrl(linkText);
        }
        if (!ogItem.getImage().isNull()) {
            if (ogItem.getImageKind() == OpenGraphItem::IconImage) {
                updated.setFavicon(ogItem.getImage());
            } else {
                updated.setThumbnail(ogItem.getImage());
            }
        }

        if (boardModel_->updateItem(row, updated)) {
            if (cardDelegate_) {
                cardDelegate_->invalidateCard(updated.getName());
            }
            if (boardService_) {
                boardService_->saveItem(updated);
            }
        }
    });
    fetcher->handle();
}

void ScrollItemsWidget::handleActivatedIndex(const QModelIndex &index) {
    setCurrentProxyIndex(index);
    const int sourceRow = sourceRowForProxyIndex(index);
    const ClipboardItem *selectedItem = selectedByEnter();
    if (selectedItem) {
        // Emit first so the handler can read the item before moveItemToFirst
        // (which reloads the model in evict mode and clears the cache).
        emit doubleClicked(*selectedItem);
        moveItemToFirst(sourceRow);
    }
}

void ScrollItemsWidget::handleLoadedItems(const QList<QPair<QString, ClipboardItem>> &items) {
    if (items.isEmpty() || !boardModel_) {
        return;
    }

    for (const auto &payload : items) {
        appendLoadedItem(payload.first, payload.second);
    }

    if (shouldEvictPages()) {
        reloadCurrentPageItems(false);
    }

    emit itemCountChanged(itemCountForDisplay());
    if (isBoardUiVisible()) {
        setFirstVisibleItemSelected();
        updateContentWidthHint();
        scheduleThumbnailUpdate();
    }
    updateLoadingOverlay();
}

void ScrollItemsWidget::handlePendingItemReady(const QString &expectedName, const ClipboardItem &item) {
    if (!boardModel_ || expectedName.isEmpty()) {
        return;
    }

    pendingThumbnailNames_.remove(expectedName);
    if (item.hasThumbnail()) {
        missingThumbnailNames_.remove(expectedName);
    } else if (usesManagedVisualPreviewCard(item)) {
        // Thumbnail generation failed — mark as missing so the card
        // falls back to text instead of staying on "Loading" forever.
        missingThumbnailNames_.insert(expectedName);
    }
    const int row = boardModel_->rowForName(expectedName);
    if (row >= 0) {
        ClipboardItem updated = item;
        if (!filterShowsVisualPreviewCards() && usesManagedVisualPreviewCard(updated)) {
            updated.setThumbnail(QPixmap());
        }
        // Invalidate the cached card so it re-renders with the new
        // thumbnail instead of showing the stale loading/unavailable state.
        if (cardDelegate_) {
            cardDelegate_->invalidateCard(expectedName);
        }
        boardModel_->updateItem(row, updated);
        syncPreviewStateForRow(row);
    }
    // The async save may have changed the service index count; keep
    // bookkeeping in sync to avoid a spurious full page reload.
    if (paginationEnabled() && loadedPageTotalItems_ >= 0) {
        loadedPageTotalItems_ = totalItemCountForPagination();
    }
    updateLoadingOverlay();
}

void ScrollItemsWidget::handleThumbnailReady(const QString &expectedName, const QPixmap &thumbnail) {
    pendingThumbnailNames_.remove(expectedName);
    if (!desiredThumbnailNames_.contains(expectedName)) {
        return;
    }
    if (!boardModel_) {
        return;
    }

    const int row = boardModel_->rowForName(expectedName);
    if (row < 0) {
        return;
    }

    const ClipboardItem *itemPtr = boardModel_->itemPtrAt(row);
    if (!itemPtr || !shouldManageThumbnail(*itemPtr)) {
        return;
    }
    if (!filterShowsVisualPreviewCards()) {
        syncPreviewStateForRow(row);
        return;
    }
    if (!thumbnail.isNull()) {
        missingThumbnailNames_.remove(expectedName);
        if (cardDelegate_) {
            cardDelegate_->invalidateCard(expectedName);
        }
        ClipboardItem updated = *itemPtr;
        updated.setThumbnail(thumbnail);
        boardModel_->updateItem(row, updated);
    } else {
        missingThumbnailNames_.insert(expectedName);
    }
    syncPreviewStateForRow(row);
}

void ScrollItemsWidget::handleKeywordMatched(const QSet<QString> &matchedNames, quint64 token) {
    if (token != keywordSearchToken_) {
        return;
    }
    asyncKeywordMatchedNames_ = matchedNames;
    applyFilters();
}

void ScrollItemsWidget::handleTotalItemCountChanged(int total) {
    Q_UNUSED(total);
    syncPageWindow(false);
    emit itemCountChanged(itemCountForDisplay());
}

void ScrollItemsWidget::handleDeferredLoadCompleted() {
    // Pre-render all cards into cardPixmapCache_ immediately so the first
    // window show is instant.  Then release intermediate data.
    preRenderAndCleanup();

    if (isBoardUiVisible()) {
        refreshContentWidthHint();
    }
    updateLoadingOverlay();
}

void ScrollItemsWidget::setFirstVisibleItemSelected() {
    if (!proxyModel_ || !listView_) {
        return;
    }

    if (listView_->selectionModel() && !listView_->selectionModel()->selectedRows().isEmpty()) {
        return;
    }

    const QModelIndex current = currentProxyIndex();
    if (current.isValid() && current.model() == proxyModel_) {
        return;
    }

    if (proxyModel_->rowCount() > 0) {
        setCurrentProxyIndex(proxyModel_->index(0, 0));
    } else if (listView_->selectionModel()) {
        listView_->selectionModel()->clearCurrentIndex();
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
    return type == Image
        || type == Office
        || (type == RichText && item.getPreviewKind() == VisualPreview);
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


void ScrollItemsWidget::updateSelectionState() {
    if (selectedItemCount() > 1) {
        hideHoverActionBar(false);
    }
    emit selectionStateChanged();
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

void ScrollItemsWidget::animateScrollTo(int targetValue) {
    QScrollBar *scrollBar = horizontalScrollbar();
    if (!scrollBar) {
        return;
    }

    targetValue = qBound(scrollBar->minimum(), targetValue, scrollBar->maximum());
    if (scrollBar->value() == targetValue) {
        return;
    }

    scrollAnimation->stop();
    const int distance = qAbs(targetValue - scrollBar->value());
    scrollAnimation->setDuration(qBound(28, 24 + distance / 12, 70));
    scrollAnimation->setStartValue(scrollBar->value());
    scrollAnimation->setEndValue(targetValue);
    scrollAnimation->start();
}

int ScrollItemsWidget::wheelStepPixels() const {
    const int viewportWidth = listView_ && listView_->viewport() ? listView_->viewport()->width() : 0;
    const int itemWidth = cardOuterSizeForScale(MPasteSettings::getInst()->getItemScale()).width();
    const int viewportStep = viewportWidth > 0 ? (viewportWidth * 2) / 5 : 480;
    const int itemStep = itemWidth > 0 ? (itemWidth * 3) / 2 : 0;
    const int scrollbarStep = listView_ ? listView_->horizontalScrollBar()->singleStep() * 8 : 0;
    return qMax(scrollbarStep, qMax(viewportStep, itemStep));
}

void ScrollItemsWidget::ensureAllItemsLoaded() {
    if (!boardService_) {
        return;
    }
    boardService_->ensureAllItemsLoaded(LOAD_BATCH_SIZE);
}

void ScrollItemsWidget::maybeLoadMoreItems() {
    if (paginationEnabled() || !boardService_ || boardService_->deferredLoadActive() || !boardService_->hasPendingItems()
        || !currentKeyword_.isEmpty() || currentTypeFilter_ != All) {
        return;
    }

    QScrollBar *scrollBar = horizontalScrollbar();
    if (!scrollBar) {
        return;
    }

    while (boardService_->hasPendingItems()) {
        const int remaining = scrollBar->maximum() - scrollBar->value();
        if (scrollBar->maximum() > 0 && remaining > LOAD_MORE_THRESHOLD_PX) {
            break;
        }

        const int pendingBefore = boardService_->pendingCount();
        boardService_->loadNextBatch(LOAD_BATCH_SIZE);
        if (boardService_->pendingCount() == pendingBefore || scrollBar->maximum() > 0) {
            break;
        }
    }
}

void ScrollItemsWidget::trimExpiredItems() {
    if (category == MPasteSettings::STAR_CATEGORY_NAME || !boardService_) {
        return;
    }
    setFirstVisibleItemSelected();
}

void ScrollItemsWidget::prepareLoadFromSaveDir() {
    if (!boardModel_ || !boardService_) {
        return;
    }

    boardModel_->clear();
    selectedItemCache_ = ClipboardItem();
    asyncKeywordMatchedNames_.clear();
    missingThumbnailNames_.clear();
    currentPage_ = 0;
    pageBaseOffset_ = 0;
    loadedPage_ = -1;
    loadedPageBaseOffset_ = -1;
    loadedPageTotalItems_ = -1;
    loadedPageTypeFilter_ = All;
    loadedPageKeyword_.clear();
    loadedPageMatchedNames_.clear();
    if (proxyModel_) {
        proxyModel_->setPageIndex(0);
    }
    boardService_->refreshIndex();
    updateContentWidthHint();
    updateLoadingOverlay();
    emit pageStateChanged(currentPageNumber(), totalPageCount());
}

bool ScrollItemsWidget::shouldKeepDeferredLoading() const {
    if (!boardService_ || !boardService_->hasPendingItems() || !currentKeyword_.isEmpty()
        || currentTypeFilter_ != All) {
        return false;
    }

    QScrollBar *scrollBar = horizontalScrollbar();
    if (!scrollBar) {
        return false;
    }

    const int remaining = scrollBar->maximum() - scrollBar->value();
    return scrollBar->maximum() <= 0 || remaining <= LOAD_MORE_THRESHOLD_PX;
}

void ScrollItemsWidget::refreshContentWidthHint() {
    updateContentWidthHint();
    updateEdgeFadeOverlays();

    QScrollBar *scrollBar = horizontalScrollbar();
    if (scrollBar) {
        scrollBar->setValue(qMin(scrollBar->value(), scrollBar->maximum()));
    }
}

void ScrollItemsWidget::updateLoadingOverlay() {
    if (!loadingLabel_ || !ui || !ui->viewHost) {
        return;
    }

    const bool hasItems = boardModel_ && boardModel_->rowCount() > 0;
    const bool isLoading = boardService_ && (boardService_->hasPendingItems() || boardService_->deferredLoadActive());
    const bool shouldShow = isLoading && !hasItems;
    loadingLabel_->setVisible(shouldShow);
    if (shouldShow) {
        const QRect hostRect = ui->viewHost->rect();
        const QSize labelSize(qMin(hostRect.width(), 260), qMin(hostRect.height(), 80));
        const QPoint topLeft((hostRect.width() - labelSize.width()) / 2,
                             (hostRect.height() - labelSize.height()) / 2);
        loadingLabel_->setGeometry(QRect(topLeft, labelSize));
        loadingLabel_->raise();
    }
}

void ScrollItemsWidget::scheduleThumbnailUpdate() {
    // No-op: cardPixmapCache_ manages all rendering.
}

void ScrollItemsWidget::primeVisibleThumbnailsSync() {
    if (!boardService_ || !boardModel_ || !proxyModel_ || !listView_ || !isBoardUiVisible()) {
        return;
    }

    const int proxyCount = proxyModel_->rowCount();
    if (proxyCount <= 0) {
        return;
    }

    const QRect viewportRect = listView_->viewport()->rect();
    const int gridWidth = qMax(1, listView_->gridSize().width());
    const int visibleCount = qMax(1, viewportRect.width() / gridWidth + 2);
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

    // Cards are rendered on-demand in paint() and cached in a bounded
    // cardPixmapCache.  No bulk pre-render needed — just clean up
    // intermediate data from the load phase.
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

void ScrollItemsWidget::updateEdgeFadeOverlays() {
    QWidget *viewport = listView_ ? listView_->viewport() : nullptr;
    QWidget *overlayHost = ui ? ui->viewHost : nullptr;
    if (!viewport || !overlayHost || !leftEdgeFadeOverlay_ || !rightEdgeFadeOverlay_) {
        return;
    }

    const int fadeWidth = qMin(edgeFadeWidth_, qMax(0, viewport->width() / 3));
    if (fadeWidth <= 0 || viewport->height() <= 0) {
        leftEdgeFadeOverlay_->hide();
        rightEdgeFadeOverlay_->hide();
        return;
    }

    const QPoint viewportTopLeft = viewport->mapTo(overlayHost, QPoint(0, 0));
    const int hostWidth = overlayHost->width();
    const int leftInset = qMax(0, viewportTopLeft.x());
    const int rightInset = qMax(0, hostWidth - (viewportTopLeft.x() + viewport->width()));
    const int leftWidth = qMin(hostWidth, fadeWidth + leftInset);
    const int rightWidth = qMin(hostWidth, fadeWidth + rightInset);
    const int overlayHeight = viewport->height();

    if (leftEdgeFadeOverlay_) {
        auto *overlay = static_cast<EdgeFadeOverlay *>(leftEdgeFadeOverlay_);
        overlay->setTransparentInset(leftInset);
        leftEdgeFadeOverlay_->setGeometry(0,
                                          viewportTopLeft.y(),
                                          leftWidth,
                                          overlayHeight);
    }
    if (rightEdgeFadeOverlay_) {
        auto *overlay = static_cast<EdgeFadeOverlay *>(rightEdgeFadeOverlay_);
        overlay->setTransparentInset(rightInset);
        rightEdgeFadeOverlay_->setGeometry(hostWidth - rightWidth,
                                           viewportTopLeft.y(),
                                           rightWidth,
                                           overlayHeight);
    }
    leftEdgeFadeOverlay_->raise();
    rightEdgeFadeOverlay_->raise();
    leftEdgeFadeOverlay_->show();
    rightEdgeFadeOverlay_->show();
}

void ScrollItemsWidget::startAsyncKeywordSearch() {
    if (currentKeyword_.isEmpty() || !boardService_) {
        return;
    }

    QList<QPair<QString, quint64>> candidates;
    const QList<ClipboardItem> items = boardModel_->items();
    for (const ClipboardItem &item : items) {
        if (item.isMimeDataLoaded() || item.sourceFilePath().isEmpty() || item.contains(currentKeyword_)) {
            continue;
        }

        const ContentType type = item.getContentType();
        if (type != Text && type != RichText) {
            continue;
        }

        candidates.append(qMakePair(item.sourceFilePath(), item.mimeDataFileOffset()));
    }

    if (candidates.isEmpty()) {
        return;
    }

    const quint64 token = keywordSearchToken_;
    boardService_->startAsyncKeywordSearch(candidates, currentKeyword_, token);
}

void ScrollItemsWidget::appendModelItem(const ClipboardItem &item) {
    if (!boardModel_ || item.getName().isEmpty()) {
        return;
    }

    const bool favorite = category == MPasteSettings::STAR_CATEGORY_NAME
        || favoriteFingerprints_.contains(item.fingerprint());
    if (item.isPinned()) {
        const int insertRow = pinnedInsertRow();
        boardModel_->insertItem(insertRow, item, favorite);
    } else {
        boardModel_->appendItem(item, favorite);
    }
    syncPreviewStateForName(item.getName());
}

bool ScrollItemsWidget::appendLoadedItem(const QString &filePath, const ClipboardItem &item) {
    Q_UNUSED(filePath);

    if (!boardModel_ || item.getName().isEmpty()) {
        return false;
    }

    if (boardModel_->rowForName(item.getName()) >= 0) {
        return false;
    }

    appendModelItem(item);
    if (!item.hasThumbnail() && shouldManageThumbnail(item)) {
        if (boardService_) {
            boardService_->processPendingItemAsync(item, item.getName());
        }
    }
    return true;
}

QPair<int, int> ScrollItemsWidget::displaySequenceForIndex(const QModelIndex &proxyIndex) const {
    if (!proxyIndex.isValid() || !proxyModel_) {
        return qMakePair(-1, itemCountForDisplay());
    }

    if (shouldEvictPages()) {
        const int sourceRow = sourceRowForProxyIndex(proxyIndex);
        return qMakePair(sourceRow >= 0 ? (pageBaseOffset_ + sourceRow + 1) : -1,
                         itemCountForDisplay());
    }

    const QModelIndex filterIndex = proxyModel_->mapToSource(proxyIndex);
    return qMakePair(filterIndex.isValid() ? (filterIndex.row() + 1) : -1,
                     itemCountForDisplay());
}

int ScrollItemsWidget::selectedItemCount() const {
    return selectedSourceRows().size();
}

bool ScrollItemsWidget::hasMultipleSelectedItems() const {
    return selectedItemCount() > 1;
}

int ScrollItemsWidget::selectedSourceRow() const {
    const QModelIndex proxyIndex = currentProxyIndex();
    if (proxyIndex.isValid() && proxyModel_) {
        return sourceRowForProxyIndex(proxyIndex);
    }

    const QList<int> selectedRows = selectedSourceRows();
    if (!selectedRows.isEmpty()) {
        return selectedRows.first();
    }
    return -1;
}

const ClipboardItem *ScrollItemsWidget::cacheSelectedItem(int sourceRow) const {
    if (!boardModel_ || sourceRow < 0) {
        return nullptr;
    }

    selectedItemCache_ = boardModel_->itemAt(sourceRow);
    return selectedItemCache_.getName().isEmpty() ? nullptr : &selectedItemCache_;
}

bool ScrollItemsWidget::isBoardUiVisible() const {
    return listView_
        && isVisible()
        && window()
        && window()->isVisible()
        && listView_->viewport()
        && listView_->viewport()->isVisible();
}

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

void ScrollItemsWidget::filterByKeyword(const QString &keyword) {
    currentKeyword_ = keyword;
    ++keywordSearchToken_;
    asyncKeywordMatchedNames_.clear();
    applyFilters();
    startAsyncKeywordSearch();
}

void ScrollItemsWidget::filterByType(ContentType type) {
    currentTypeFilter_ = type;
    applyFilters();
}

QList<QModelIndex> ScrollItemsWidget::shortcutVisibleIndexes() const {
    QList<QModelIndex> result;
    if (!listView_ || !proxyModel_ || !listView_->viewport()) {
        return result;
    }

    const QRect viewportRect = listView_->viewport()->rect();
    if (!viewportRect.isValid()) {
        return result;
    }

    const int rowCount = proxyModel_->rowCount();
    if (rowCount <= 0) {
        return result;
    }

    const int midY = viewportRect.center().y();
    const QPoint probe(qMax(0, viewportRect.left() + 1), midY);
    QModelIndex startIndex = listView_->indexAt(probe);
    int startRow = startIndex.isValid() ? startIndex.row() : 0;
    if (startRow < 0 || startRow >= rowCount) {
        startRow = 0;
    }

    int firstFullRow = -1;
    for (int row = startRow; row < rowCount; ++row) {
        const QModelIndex idx = proxyModel_->index(row, 0);
        const QRect rect = listView_->visualRect(idx);
        if (!rect.isValid()) {
            continue;
        }
        if (rect.left() > viewportRect.right()) {
            break;
        }
        if (viewportRect.contains(rect)) {
            firstFullRow = row;
            break;
        }
    }

    if (firstFullRow < 0) {
        for (int row = startRow; row < rowCount && result.size() < 10; ++row) {
            result.append(proxyModel_->index(row, 0));
        }
        return result;
    }

    for (int row = firstFullRow; row < rowCount && result.size() < 10; ++row) {
        const QModelIndex idx = proxyModel_->index(row, 0);
        const QRect rect = listView_->visualRect(idx);
        if (!rect.isValid()) {
            continue;
        }
        if (rect.left() > viewportRect.right()) {
            break;
        }
        if (!viewportRect.contains(rect)) {
            continue;
        }
        result.append(idx);
    }

    return result;
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

void ScrollItemsWidget::setShortcutInfo() {
    cleanShortCutInfo();
    if (!proxyModel_ || !boardModel_) {
        return;
    }

    const QList<QModelIndex> indexes = shortcutVisibleIndexes();
    for (int i = 0; i < indexes.size(); ++i) {
        const QModelIndex proxyIndex = indexes.at(i);
        if (!proxyIndex.isValid()) {
            continue;
        }
        const int sourceRow = sourceRowForProxyIndex(proxyIndex);
        if (sourceRow < 0) {
            continue;
        }
        boardModel_->setShortcutText(sourceRow, QStringLiteral("Alt+%1").arg((i + 1) % 10));
    }
}

void ScrollItemsWidget::cleanShortCutInfo() {
    if (boardModel_) {
        boardModel_->clearShortcutTexts();
    }
}

void ScrollItemsWidget::loadFromSaveDir() {
    if (boardService_) {
        boardService_->stopDeferredLoad();
    }
    prepareLoadFromSaveDir();
    if (boardService_) {
        const int initialBatchSize = paginationEnabled() ? PAGED_INITIAL_LOAD_BATCH_SIZE
                                                         : CONTINUOUS_INITIAL_LOAD_BATCH_SIZE;
        boardService_->startAsyncLoad(initialBatchSize, 0);
    }
}

void ScrollItemsWidget::applyScale(int scale) {
    if (!listView_ || !ui) {
        return;
    }

    hideHoverActionBar(false);
    if (hoverActionBar_) {
        hoverActionBar_->deleteLater();
        hoverActionBar_ = nullptr;
    }
    if (hoverHideTimer_) {
        hoverHideTimer_->stop();
        hoverHideTimer_->deleteLater();
        hoverHideTimer_ = nullptr;
    }
    hoverDetailsBtn_ = nullptr;
    hoverAliasBtn_ = nullptr;
    hoverPinBtn_ = nullptr;
    hoverFavoriteBtn_ = nullptr;
    hoverDeleteBtn_ = nullptr;
    hoverOpacity_ = nullptr;

    const QSize itemOuterSize = cardOuterSizeForScale(scale);
    const int spacing = qMax(6, 8 * scale / 100);
    listView_->setSpacing(spacing);
    listView_->setGridSize(QSize(itemOuterSize.width() + spacing, itemOuterSize.height()));

    const int scrollbarHeight = qMax(12, listView_->horizontalScrollBar()->sizeHint().height());
    const int scrollHeight = itemOuterSize.height() + scrollbarHeight;
    edgeContentPadding_ = qMax(6, 8 * scale / 100);
    edgeFadeWidth_ = qMax(12, 16 * scale / 100);
    if (auto *boardView = dynamic_cast<ClipboardBoardView *>(listView_)) {
        boardView->applyViewportMargins(edgeContentPadding_, 0, edgeContentPadding_, 0);
    }
    listView_->setFixedHeight(scrollHeight);
    ui->viewHost->setFixedHeight(scrollHeight);
    setFixedHeight(scrollHeight);

    createHoverActionBar();
    applyTheme(darkTheme_);
    refreshContentWidthHint();
}

void ScrollItemsWidget::loadFromSaveDirDeferred() {
    if (boardService_) {
        boardService_->stopDeferredLoad();
    }
    prepareLoadFromSaveDir();

    if (!boardService_) {
        emit itemCountChanged(itemCountForDisplay());
        return;
    }

    if (paginationEnabled()) {
        boardService_->startAsyncLoad(PAGED_INITIAL_LOAD_BATCH_SIZE, 0);
    } else {
        boardService_->startAsyncLoad(CONTINUOUS_INITIAL_LOAD_BATCH_SIZE,
                                      CONTINUOUS_DEFERRED_LOAD_BATCH_SIZE);
    }
}

void ScrollItemsWidget::syncFromDiskIncremental() {
    if (!boardService_ || !boardModel_) {
        return;
    }

    const auto syncResult = boardService_->syncIndexIncremental();
    if (syncResult.addedPaths.isEmpty() && syncResult.removedPaths.isEmpty()) {
        return;
    }

    // Detect renames: a removed path and an added path that share the
    // same fingerprint represent a moveItemToFirst rename, not a true
    // delete + create.  For these pairs, update the model item in place
    // so the existing row order is preserved.
    // Remove items that no longer exist on disk.
    for (const QString &path : syncResult.removedPaths) {
        const QString name = QFileInfo(path).completeBaseName();
        const int row = boardModel_->rowForName(name);
        if (row >= 0) {
            if (cardDelegate_) {
                cardDelegate_->invalidateCard(name);
            }
            boardModel_->removeItemAt(row);
        }
    }

    // Add new items from disk.
    for (const QString &path : syncResult.addedPaths) {
        ClipboardItem item = boardService_->loadItemLight(path, true);
        if (item.getName().isEmpty()) {
            continue;
        }
        if (boardModel_->rowForName(item.getName()) >= 0) {
            continue;
        }

        const bool favorite = category == MPasteSettings::STAR_CATEGORY_NAME
            || favoriteFingerprints_.contains(item.fingerprint());
        if (item.isPinned()) {
            const int insertRow = pinnedInsertRow();
            boardModel_->insertItem(insertRow, item, favorite);
        } else {
            // Find the correct row: items are sorted newest-first by time.
            // Skip past pinned items, then find where this item fits.
            const int startRow = pinnedInsertRow();
            int insertRow = startRow;
            for (int r = startRow; r < boardModel_->rowCount(); ++r) {
                const ClipboardItem *existing = boardModel_->itemPtrAt(r);
                if (existing && item.getTime() > existing->getTime()) {
                    break;
                }
                insertRow = r + 1;
            }
            boardModel_->insertItem(insertRow, item, favorite);
        }
        syncPreviewStateForName(item.getName());
    }

    applyFilters();
    emit itemCountChanged(itemCountForDisplay());
}

QString ScrollItemsWidget::memoryStats() const {
    QStringList lines;
    const QString cat = category;

    // Model items
    int modelRows = 0;
    int mimeLoadedCount = 0;
    qint64 thumbnailBytes = 0;
    qint64 iconBytes = 0;
    qint64 faviconBytes = 0;
    int thumbnailCount = 0;
    if (boardModel_) {
        modelRows = boardModel_->rowCount();
        for (int i = 0; i < modelRows; ++i) {
            const ClipboardItem *item = boardModel_->itemPtrAt(i);
            if (!item) continue;
            if (item->isMimeDataLoaded()) ++mimeLoadedCount;
            if (item->hasThumbnail()) {
                const QPixmap t = item->thumbnail();
                thumbnailBytes += static_cast<qint64>(t.width()) * t.height() * 4;
                ++thumbnailCount;
            }
            if (!item->getIcon().isNull()) {
                const QPixmap ic = item->getIcon();
                iconBytes += static_cast<qint64>(ic.width()) * ic.height() * 4;
            }
            if (!item->getFavicon().isNull()) {
                const QPixmap fv = item->getFavicon();
                faviconBytes += static_cast<qint64>(fv.width()) * fv.height() * 4;
            }
        }
    }

    // Card pixmap cache
    int cacheCount = 0;
    int cacheMaxCost = 0;
    if (cardDelegate_) {
        cacheCount = cardDelegate_->cachedCardCount();
        cacheMaxCost = cardDelegate_->cachedCardMaxCost();
    }

    lines << QStringLiteral("[%1] model: %2 items, mimeData loaded: %3")
                 .arg(cat).arg(modelRows).arg(mimeLoadedCount);
    lines << QStringLiteral("[%1] thumbnails: %2 (%3 KB), icons: %4 KB, favicons: %5 KB")
                 .arg(cat).arg(thumbnailCount).arg(thumbnailBytes / 1024).arg(iconBytes / 1024).arg(faviconBytes / 1024);
    lines << QStringLiteral("[%1] cardCache: %2 / %3")
                 .arg(cat).arg(cacheCount).arg(cacheMaxCost);
    if (cardDelegate_) {
        lines << cardDelegate_->cacheMemoryStats();
    }

    return lines.join(QLatin1Char('\n'));
}

QScrollBar *ScrollItemsWidget::horizontalScrollbar() const {
    return listView_ ? listView_->horizontalScrollBar() : nullptr;
}

void ScrollItemsWidget::setAllItemVisible() {
    currentKeyword_.clear();
    currentTypeFilter_ = All;
    asyncKeywordMatchedNames_.clear();
    applyFilters();
}

const ClipboardItem *ScrollItemsWidget::currentSelectedItem() const {
    return cacheSelectedItem(selectedSourceRow());
}

const ClipboardItem *ScrollItemsWidget::selectedByShortcut(int keyIndex) {
    if (!proxyModel_ || keyIndex < 0) {
        return currentSelectedItem();
    }

    const QList<QModelIndex> indexes = shortcutVisibleIndexes();
    if (keyIndex >= indexes.size()) {
        return currentSelectedItem();
    }

    const QModelIndex proxyIndex = indexes.at(keyIndex);
    if (!proxyIndex.isValid()) {
        return currentSelectedItem();
    }
    setCurrentProxyIndex(proxyIndex);
    return cacheSelectedItem(sourceRowForProxyIndex(proxyIndex));
}

const ClipboardItem *ScrollItemsWidget::selectedByEnter() {
    return currentSelectedItem();
}

void ScrollItemsWidget::hideHoverTools() {
    hoverProxyIndex_ = QPersistentModelIndex();
    hideHoverActionBar(false);
}

void ScrollItemsWidget::focusMoveLeft() {
    if (!proxyModel_ || proxyModel_->rowCount() <= 0) {
        return;
    }

    QModelIndex current = currentProxyIndex();
    if (!current.isValid()) {
        setCurrentProxyIndex(proxyModel_->index(0, 0));
        return;
    }

    const QModelIndex nextIndex = proxyModel_->index(qMax(0, current.row() - 1), 0);
    setCurrentProxyIndex(nextIndex);
    if (auto *bv = dynamic_cast<ClipboardBoardView *>(listView_)) bv->setExplicitScrollTo(true);
    listView_->scrollTo(nextIndex, QAbstractItemView::EnsureVisible);
}

void ScrollItemsWidget::focusMoveRight() {
    if (!proxyModel_ || proxyModel_->rowCount() <= 0) {
        return;
    }

    QModelIndex current = currentProxyIndex();
    if (!current.isValid()) {
        setCurrentProxyIndex(proxyModel_->index(0, 0));
        return;
    }

    const QModelIndex nextIndex = proxyModel_->index(qMin(proxyModel_->rowCount() - 1, current.row() + 1), 0);
    setCurrentProxyIndex(nextIndex);
    if (auto *bv = dynamic_cast<ClipboardBoardView *>(listView_)) bv->setExplicitScrollTo(true);
    listView_->scrollTo(nextIndex, QAbstractItemView::EnsureVisible);
}

int ScrollItemsWidget::getItemCount() {
    return itemCountForDisplay();
}

QSet<QByteArray> ScrollItemsWidget::loadAllFingerprints() {
    if (!boardService_) {
        return {};
    }
    return boardService_->loadAllFingerprints();
}

void ScrollItemsWidget::setFavoriteFingerprints(const QSet<QByteArray> &fingerprints) {
    favoriteFingerprints_ = fingerprints;
    if (!boardModel_) {
        return;
    }

    const int rowCount = boardModel_->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (!item) {
            continue;
        }
        const bool favorite = favoriteFingerprints_.contains(item->fingerprint());
        if (boardModel_->isFavorite(row) != favorite) {
            boardModel_->setFavoriteByFingerprint(item->fingerprint(), favorite);
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

void ScrollItemsWidget::scrollToFirst() {
    if (!proxyModel_) {
        return;
    }

    if (paginationEnabled() && currentPage_ != 0) {
        setCurrentPageNumber(1);
    }

    if (proxyModel_->rowCount() <= 0) {
        return;
    }

    setCurrentProxyIndex(proxyModel_->index(0, 0));
    animateScrollTo(0);
}

void ScrollItemsWidget::scrollToLast() {
    if (!proxyModel_ || proxyModel_->rowCount() <= 0) {
        return;
    }

    setCurrentProxyIndex(proxyModel_->index(proxyModel_->rowCount() - 1, 0));
    animateScrollTo(horizontalScrollbar() ? horizontalScrollbar()->maximum() : 0);
}

QString ScrollItemsWidget::getCategory() const {
    return category;
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

QList<ClipboardItem> ScrollItemsWidget::allItems() {
    ensureAllItemsLoaded();
    return boardModel_->items();
}

bool ScrollItemsWidget::handleWheelScroll(QWheelEvent *event) {
    if (!event) {
        return false;
    }

    QScrollBar *scrollBar = horizontalScrollbar();
    if (!scrollBar || (scrollBar->maximum() <= 0 && (!boardService_ || !boardService_->hasPendingItems()))) {
        return false;
    }

    int delta = 0;
    const QPoint pixelDelta = event->pixelDelta();
    if (!pixelDelta.isNull()) {
        if (qAbs(pixelDelta.x()) < qAbs(pixelDelta.y()) || pixelDelta.x() == 0) {
            return false;
        }
        delta = qRound((-pixelDelta.x()) * 1.6);
    } else {
        const QPoint angleDelta = event->angleDelta();
        const int dominantDelta = qAbs(angleDelta.x()) > qAbs(angleDelta.y()) ? angleDelta.x() : angleDelta.y();
        if (dominantDelta != 0) {
            delta = qRound((-dominantDelta / 120.0) * wheelStepPixels());
        }
    }

    if (delta == 0) {
        return false;
    }

    animateScrollTo(scrollBar->value() + delta);
    event->accept();
    return true;
}

void ScrollItemsWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refreshContentWidthHint();
    updateEdgeFadeOverlays();
    if (boardService_) {
        boardService_->setVisibleHint(isVisible() && window() && window()->isVisible());
    }
    setFirstVisibleItemSelected();
    scheduleThumbnailUpdate();
    updateLoadingOverlay();
}

void ScrollItemsWidget::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    hideHoverActionBar(false);
    if (boardService_) {
        boardService_->setVisibleHint(false);
    }
}

void ScrollItemsWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    refreshContentWidthHint();
    scheduleThumbnailUpdate();
    updateLoadingOverlay();
}

bool ScrollItemsWidget::eventFilter(QObject *watched, QEvent *event) {
    if (!listView_) {
        return QWidget::eventFilter(watched, event);
    }

    const bool watchingHoverBar = hoverActionBar_
        && (watched == hoverActionBar_
            || watched == hoverDetailsBtn_
            || watched == hoverAliasBtn_
            || watched == hoverPinBtn_
            || watched == hoverFavoriteBtn_
            || watched == hoverDeleteBtn_);

    if (watchingHoverBar) {
        if (event->type() == QEvent::Enter
            || event->type() == QEvent::HoverEnter
            || event->type() == QEvent::MouseMove) {
            if (hoverHideTimer_ && hoverHideTimer_->isActive()) {
                hoverHideTimer_->stop();
            }
            return QWidget::eventFilter(watched, event);
        }

        if (event->type() == QEvent::Leave || event->type() == QEvent::HoverLeave) {
            if (hoverActionBar_ && hoverHideTimer_) {
                const QPoint hoverPoint = hoverActionBar_->mapFromGlobal(QCursor::pos());
                if (!hoverActionBar_->rect().contains(hoverPoint)) {
                    hoverHideTimer_->start();
                }
            }
            return QWidget::eventFilter(watched, event);
        }
    }

    if (event->type() == QEvent::Wheel && (watched == listView_ || watched == listView_->viewport())) {
        const bool handled = handleWheelScroll(static_cast<QWheelEvent *>(event));
        if (handled) {
            updateHoverActionBarPosition();
            scheduleThumbnailUpdate();
        }
        return handled;
    }

    if (event->type() == QEvent::Resize && watched == listView_->viewport()) {
        refreshContentWidthHint();
        updateHoverActionBarPosition();
        scheduleThumbnailUpdate();
        updateLoadingOverlay();
    }

    if ((watched == listView_ || watched == listView_->viewport()) && event->type() == QEvent::MouseMove) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (hoverActionBar_ && hoverActionBar_->isVisible()) {
            auto *watchedWidget = qobject_cast<QWidget *>(watched);
            const QPoint hoverPoint = watchedWidget && hoverActionBar_->parentWidget()
                ? watchedWidget->mapTo(hoverActionBar_->parentWidget(), mouseEvent->pos())
                : mouseEvent->pos();
            if (hoverActionBar_->geometry().contains(hoverPoint)) {
                return QWidget::eventFilter(watched, event);
            }
        }
        const QModelIndex hoverIndex = listView_->indexAt(mouseEvent->pos());
        updateHoverActionBar(hoverIndex);
        return QWidget::eventFilter(watched, event);
    }

    if ((watched == listView_ || watched == listView_->viewport())
        && (event->type() == QEvent::Leave || event->type() == QEvent::HoverLeave)) {
        if (hoverHideTimer_ && !hoverHideTimer_->isActive()) {
            hoverHideTimer_->start();
        }
    }

    if (event->type() == QEvent::KeyPress) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Delete && selectedItemCount() > 0) {
            const QList<ClipboardItem> selection = selectedItems();
            if (category == MPasteSettings::STAR_CATEGORY_NAME) {
                applyFavoriteToItems(selection, false);
            } else {
                removeItems(selection);
            }
            return true;
        }
        QGuiApplication::sendEvent(parent(), event);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
