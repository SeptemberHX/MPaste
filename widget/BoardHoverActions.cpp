// input: Depends on ScrollItemsWidget.h, BoardInternalHelpers.h, BoardViewWidgets.h, Qt Widgets.
// output: Implements hover action bar management methods of ScrollItemsWidget.
// pos: Split from ScrollItemsWidgetMV.cpp -- hover action bar creation and positioning.
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>

#include <utils/MPasteSettings.h>

#include "BoardInternalHelpers.h"
#include "BoardViewWidgets.h"
#include "CardMetrics.h"
#include "ClipboardBoardActionService.h"
#include "ClipboardBoardModel.h"
#include "ClipboardBoardProxyModel.h"
#include "ClipboardItemRenameDialog.h"
#include "ScrollItemsWidget.h"
#include "ui_ScrollItemsWidget.h"
#include "utils/IconResolver.h"

using namespace BoardHelpers;

void ScrollItemsWidget::createHoverActionBar() {
    if (!listView_ || !ui || !ui->viewHost) {
        return;
    }

    const int scale = MPasteSettings::getInst()->getItemScale();
    const int barHeight = qMax(20, 28 * scale / 100);
    const int borderRadius = qMax(6, 8 * scale / 100);
    const int margin = qMax(4, 6 * scale / 100);
    const int spacing = qMax(2, 4 * scale / 100);
    const bool dark = ThemeManager::instance()->isDark();

    auto *hoverBar = new HoverActionBar(ui->viewHost);
    hoverActionBar_ = hoverBar;
    hoverActionBar_->setObjectName(QStringLiteral("cardActionBar"));
    hoverActionBar_->setFixedHeight(barHeight);
    hoverActionBar_->setFocusPolicy(Qt::NoFocus);
    hoverActionBar_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    hoverActionBar_->setAttribute(Qt::WA_TranslucentBackground);
    hoverActionBar_->setMouseTracking(true);
    hoverActionBar_->installEventFilter(this);
    hoverBar->setCornerRadius(borderRadius);
    hoverBar->setColors(dark ? QColor(26, 31, 38, 205) : QColor(255, 255, 255, 185),
                        dark ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 120));

    hoverOpacity_ = new QGraphicsOpacityEffect(hoverActionBar_);
    hoverOpacity_->setOpacity(0.0);
    hoverActionBar_->setGraphicsEffect(hoverOpacity_);

    auto *layout = new QHBoxLayout(hoverActionBar_);
    layout->setContentsMargins(margin, 0, margin, 0);
    layout->setSpacing(spacing);

    auto createButton = [this, scale, dark](const QString &iconPath, const QString &tooltip) {
        const int iconSz = qMax(12, 14 * scale / 100);
        const int btnSz = qMax(18, 22 * scale / 100);
        const int borderR = qMax(4, 6 * scale / 100);
        const int pad = qMax(2, 3 * scale / 100);

        auto *button = new QToolButton;
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(iconSz, iconSz));
        button->setFixedSize(btnSz, btnSz);
        button->setStyleSheet(hoverButtonStyle(dark, borderR, pad));
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tooltip);
        button->setFocusPolicy(Qt::NoFocus);
        button->setMouseTracking(true);
        button->installEventFilter(this);
        return button;
    };

    hoverDetailsBtn_ = createButton(IconResolver::themedPath(QStringLiteral("details"), dark), detailsLabel());
    hoverAliasBtn_ = createButton(IconResolver::themedPath(QStringLiteral("rename"), dark), aliasLabel());
    hoverPinBtn_ = createButton(IconResolver::themedPath(QStringLiteral("pin"), dark), pinActionLabel(false));
    hoverFavoriteBtn_ = createButton(IconResolver::themedPath(QStringLiteral("star_outline"), dark), favoriteActionLabel(false));
    hoverDeleteBtn_ = createButton(QStringLiteral(":/resources/resources/delete.svg"), deleteLabel());

    layout->addWidget(hoverDetailsBtn_);
    layout->addWidget(hoverAliasBtn_);
    layout->addWidget(hoverPinBtn_);
    layout->addWidget(hoverFavoriteBtn_);
    layout->addWidget(hoverDeleteBtn_);

    connect(hoverDetailsBtn_, &QToolButton::clicked, this, [this]() {
        if (!proxyModel_ || !boardModel_ || !hoverProxyIndex_.isValid()) {
            return;
        }
        const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item) {
            return;
        }
        const auto seq = displaySequenceForIndex(hoverProxyIndex_);
        emit detailsRequested(*item, seq.first, seq.second);
    });

    connect(hoverAliasBtn_, &QToolButton::clicked, this, [this]() {
        if (!proxyModel_ || !boardModel_ || !hoverProxyIndex_.isValid()) {
            return;
        }
        const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item) {
            return;
        }
        openAliasDialogForItem(*item);
        hideHoverActionBar(false);
    });

    connect(hoverPinBtn_, &QToolButton::clicked, this, [this]() {
        if (!proxyModel_ || !boardModel_ || !hoverProxyIndex_.isValid()) {
            return;
        }
        const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item) {
            return;
        }
        setItemPinned(*item, !item->isPinned());
        updateHoverPinButton(!item->isPinned());
    });

    connect(hoverFavoriteBtn_, &QToolButton::clicked, this, [this]() {
        if (!proxyModel_ || !boardModel_ || !hoverProxyIndex_.isValid()) {
            return;
        }
        const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item) {
            return;
        }
        const bool isFavorite = boardModel_->isFavorite(sourceRow);
        setItemFavorite(*item, !isFavorite);
        if (isFavorite) {
            emit itemUnstared(*item);
        } else {
            emit itemStared(*item);
        }
        updateHoverFavoriteButton(!isFavorite);
    });

    connect(hoverDeleteBtn_, &QToolButton::clicked, this, [this]() {
        if (!proxyModel_ || !boardModel_ || !hoverProxyIndex_.isValid()) {
            return;
        }
        const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item) {
            return;
        }
        removeItemByContent(*item);
        hideHoverActionBar(false);
    });

    hoverActionBar_->hide();

    hoverHideTimer_ = new QTimer(this);
    hoverHideTimer_->setSingleShot(true);
    hoverHideTimer_->setInterval(80);
    connect(hoverHideTimer_, &QTimer::timeout, this, [this]() {
        hideHoverActionBar(true);
    });
}

void ScrollItemsWidget::updateHoverFavoriteButton(bool favorite) {
    if (!hoverFavoriteBtn_) {
        return;
    }
    if (favorite) {
        hoverFavoriteBtn_->setIcon(QIcon(QStringLiteral(":/resources/resources/star_filled.svg")));
    } else {
        hoverFavoriteBtn_->setIcon(IconResolver::themedIcon(QStringLiteral("star_outline"), darkTheme_));
    }
    hoverFavoriteBtn_->setToolTip(favoriteActionLabel(favorite));
}

void ScrollItemsWidget::updateHoverPinButton(bool pinned) {
    if (!hoverPinBtn_) {
        return;
    }
    hoverPinBtn_->setToolTip(pinActionLabel(pinned));
}

void ScrollItemsWidget::updateHoverActionBar(const QModelIndex &proxyIndex) {
    if (!hoverActionBar_ || !listView_ || !proxyModel_ || !boardModel_) {
        return;
    }

    if (selectedItemCount() > 1) {
        hideHoverActionBar(false);
        return;
    }

    if (!proxyIndex.isValid()) {
        if (hoverHideTimer_ && !hoverHideTimer_->isActive()) {
            hoverHideTimer_->start();
        }
        return;
    }

    if (hoverHideTimer_ && hoverHideTimer_->isActive()) {
        hoverHideTimer_->stop();
    }

    const int sourceRow = sourceRowForProxyIndex(proxyIndex);
    const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
    if (!item || item->getName().isEmpty()) {
        if (hoverHideTimer_ && !hoverHideTimer_->isActive()) {
            hoverHideTimer_->start();
        }
        return;
    }

    hoverProxyIndex_ = proxyIndex;
    updateHoverFavoriteButton(boardModel_->isFavorite(sourceRow));
    updateHoverPinButton(item->isPinned());
    updateHoverActionBarPosition();

    if (!hoverActionBar_->isVisible()) {
        hoverActionBar_->show();
        hoverActionBar_->raise();
        if (hoverOpacity_) {
            auto *anim = new QPropertyAnimation(hoverOpacity_, "opacity", hoverActionBar_);
            anim->setDuration(50);
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
}

void ScrollItemsWidget::updateHoverActionBarPosition() {
    if (!hoverActionBar_ || !listView_ || !hoverProxyIndex_.isValid()) {
        return;
    }

    if (!listView_->model() || hoverProxyIndex_.model() != listView_->model()) {
        hideHoverActionBar(false);
        return;
    }

    QWidget *viewport = listView_->viewport();
    QWidget *overlayHost = hoverActionBar_->parentWidget();
    if (!viewport || !overlayHost) {
        hideHoverActionBar(false);
        return;
    }

    const QRect itemRect = listView_->visualRect(hoverProxyIndex_);
    const QRect viewportRect = viewport->rect();
    if (!itemRect.isValid() || !viewportRect.intersects(itemRect)) {
        hideHoverActionBar();
        return;
    }

    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize cardSize(kCardBaseWidth * scale / 100, kCardBaseHeight * scale / 100);
    const QRect cardRect(itemRect.topLeft(), cardSize);
    const QPoint hostTopLeft = viewport->mapTo(overlayHost, cardRect.topLeft());
    hoverActionBar_->adjustSize();
    if (overlayHost->width() <= 0 || overlayHost->height() <= 0
        || hoverActionBar_->width() <= 0 || hoverActionBar_->height() <= 0) {
        hideHoverActionBar(false);
        return;
    }

    const int desiredX = hostTopLeft.x() + (cardRect.width() - hoverActionBar_->width()) / 2;
    const int desiredY = hostTopLeft.y() + qMax(2, 4 * scale / 100);
    const int maxX = qMax(0, overlayHost->width() - hoverActionBar_->width());
    const int maxY = qMax(0, overlayHost->height() - hoverActionBar_->height());
    const int x = qBound(0, desiredX, maxX);
    const int y = qBound(0, desiredY, maxY);
    hoverActionBar_->move(x, y);
}

void ScrollItemsWidget::hideHoverActionBar(bool animated) {
    if (!hoverActionBar_ || !hoverActionBar_->isVisible()) {
        return;
    }
    if (hoverHideTimer_ && hoverHideTimer_->isActive()) {
        hoverHideTimer_->stop();
    }
    if (!animated || !hoverOpacity_) {
        hoverActionBar_->hide();
        return;
    }
    auto *anim = new QPropertyAnimation(hoverOpacity_, "opacity", hoverActionBar_);
    anim->setDuration(50);
    anim->setStartValue(hoverOpacity_->opacity());
    anim->setEndValue(0.0);
    connect(anim, &QPropertyAnimation::finished, hoverActionBar_, &QWidget::hide);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
