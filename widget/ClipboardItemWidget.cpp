// input: Depends on ClipboardItemWidget.h, Qt Widgets runtime, and icon/resource assets.
// output: Implements item-card actions, hover UI, context menu, and plain-text paste trigger.
// pos: Widget-layer item card implementation used by history boards.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardItemWidget.h"

#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QToolButton>
#include <QMenu>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>

#include "utils/MPasteSettings.h"

namespace {
bool looksBrokenTranslation(const QString &text) {
    if (text.isEmpty()) {
        return true;
    }

    int suspiciousCount = 0;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('?') || ch == QChar::ReplacementCharacter) {
            ++suspiciousCount;
        }
    }

    return suspiciousCount >= qMax(2, text.size() / 2);
}

QString plainTextPasteLabel() {
    QString label = QObject::tr("Paste as Plain Text");
    if (label == QLatin1String("Paste as Plain Text") || looksBrokenTranslation(label)) {
        if (QLocale::system().language() == QLocale::Chinese) {
            return QString::fromUtf16(u"\u7EAF\u6587\u672C\u7C98\u8D34");
        }
        return QStringLiteral("Paste as Plain Text");
    }
    return label;
}
}

ClipboardItemWidget::ClipboardItemWidget(QString category, QColor borderColor, QWidget *parent)
    : QWidget(parent), category(category), borderColor(borderColor)
{
    setupUI();
    setupActionButtons();
    setupContextMenu();
    initializeEffects();
}

void ClipboardItemWidget::setupUI() {
    // Widget attributes
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setAttribute(Qt::WA_Hover);
    setAutoFillBackground(false);

    // Main layout
    ui.mainLayout = new QHBoxLayout(this);
    ui.mainLayout->setContentsMargins(0, 0, 3, 0);

    // Inner widget
    ui.innerWidget = new ClipboardItemInnerWidget(this->borderColor, this);
    ui.innerWidget->setObjectName("innerWidget");
    connect(ui.innerWidget, &ClipboardItemInnerWidget::itemNeedToSave, 
            this, [this](const ClipboardItem &item) {
        currentItem = item;
        Q_EMIT itemNeedToSave();
    });

    ui.mainLayout->addWidget(ui.innerWidget);

    // Persistent favorite indicator (small star in top-right corner)
    int scale = MPasteSettings::getInst()->getItemScale();
    int indicatorSz = 16 * scale / 100;
    ui.favoriteIndicator = new QLabel(this);
    ui.favoriteIndicator->setFixedSize(indicatorSz, indicatorSz);
    ui.favoriteIndicator->setPixmap(
        QPixmap(":/resources/resources/star_filled.svg")
            .scaled(indicatorSz, indicatorSz, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui.favoriteIndicator->setStyleSheet("background: transparent; border: none;");
    ui.favoriteIndicator->hide();
}

void ClipboardItemWidget::setupActionButtons() {
    int scale = MPasteSettings::getInst()->getItemScale();

    // Container setup
    ui.actions.container = new QWidget(this);
    ui.actions.container->setFixedHeight(28 * scale / 100);
    int borderR = 8 * scale / 100;
    ui.actions.container->setStyleSheet(QString(R"(
        background: rgba(255, 255, 255, 0.9);
        border: 1px solid rgba(0, 0, 0, 0.08);
        border-radius: %1px;
    )").arg(borderR));
    ui.actions.container->hide();

    auto *opacityEffect = new QGraphicsOpacityEffect(ui.actions.container);
    opacityEffect->setOpacity(0.0);
    ui.actions.container->setGraphicsEffect(opacityEffect);

    int margin = 6 * scale / 100;
    ui.actions.layout = new QHBoxLayout(ui.actions.container);
    ui.actions.layout->setContentsMargins(margin, 0, margin, 0);
    ui.actions.layout->setSpacing(4 * scale / 100);

    // Create buttons
    ui.actions.favoriteBtn = createActionButton(
        ":/resources/resources/star_outline.svg",
        tr("Add to favorites")
    );
    ui.actions.deleteBtn = createActionButton(
        ":/resources/resources/delete.svg",
        tr("Delete")
    );

    // Add buttons to layout
    ui.actions.layout->addWidget(ui.actions.favoriteBtn);
    ui.actions.layout->addWidget(ui.actions.deleteBtn);

    // Connect signals
    connect(ui.actions.favoriteBtn, &QToolButton::clicked, this, &ClipboardItemWidget::handleFavoriteAction);
    connect(ui.actions.deleteBtn, &QToolButton::clicked, this, &ClipboardItemWidget::handleDeleteAction);
}

void ClipboardItemWidget::setupContextMenu() {
    ui.contextMenu = new QMenu(this);

    auto addAction = [this](const QString& iconPath, const QString& text, auto slot) {
        QAction* action = new QAction(QIcon(iconPath), text, this);
        connect(action, &QAction::triggered, this, slot);
        ui.contextMenu->addAction(action);
        return action;
    };

    addAction(":/resources/resources/save_black.svg", tr("Save"), &ClipboardItemWidget::handleSaveAction);
    addAction(":/resources/resources/files.svg", plainTextPasteLabel(), &ClipboardItemWidget::handlePastePlainTextAction);
    addAction(":/resources/resources/preview.svg", tr("Preview"), &ClipboardItemWidget::previewRequested);

    ui.contextMenu->addSeparator();

    if (this->category != MPasteSettings::STAR_CATEGORY_NAME) {
        addAction(":/resources/resources/star.svg", tr("Save to Star"), &ClipboardItemWidget::handleStarAction);
    }
    addAction(":/resources/resources/add_black.svg", tr("Save to"), &ClipboardItemWidget::handleFavoriteAction);

    addAction(":/resources/resources/delete.svg", tr("Delete"), &ClipboardItemWidget::handleDeleteAction);
}


void ClipboardItemWidget::initializeEffects() {
    auto* shadowEffect = new QGraphicsDropShadowEffect(this);
    shadowEffect->setOffset(3, 4);
    shadowEffect->setColor(QColor(0, 0, 0, 30));
    shadowEffect->setBlurRadius(8);
    ui.innerWidget->setGraphicsEffect(shadowEffect);
}

QToolButton* ClipboardItemWidget::createActionButton(const QString& iconPath, const QString& tooltip) const {
    int scale = MPasteSettings::getInst()->getItemScale();
    int iconSz = 14 * scale / 100;
    int btnSz = 22 * scale / 100;
    int borderR = 6 * scale / 100;
    int pad = 3 * scale / 100;

    auto* button = new QToolButton;
    button->setIcon(QIcon(iconPath));
    button->setIconSize(QSize(iconSz, iconSz));
    button->setFixedSize(btnSz, btnSz);
    button->setStyleSheet(QString(R"(
        QToolButton {
            background: transparent;
            border: none;
            border-radius: %1px;
            padding: %2px;
        }
        QToolButton:hover {
            background: rgba(0, 0, 0, 0.08);
        }
    )").arg(borderR).arg(pad));
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    return button;
}

void ClipboardItemWidget::showContextMenu(const QPoint& pos) {
    ui.contextMenu->exec(mapToGlobal(pos));
}

void ClipboardItemWidget::handleFavoriteAction() {
    isFavorite = !isFavorite;
    updateFavoriteButton();
    emit favoriteChanged(isFavorite);
    if (isFavorite) {
        emit itemStared(currentItem);
    } else {
        emit itemUnstared(currentItem);
    }
}

void ClipboardItemWidget::setFavorite(bool favorite) {
    isFavorite = favorite;
    updateFavoriteButton();
}

void ClipboardItemWidget::handleDeleteAction() {
    emit deleteRequested();
}

void ClipboardItemWidget::handleSaveAction() {
    emit saveRequested(currentItem);
}

void ClipboardItemWidget::handlePastePlainTextAction() {
    emit pastePlainTextRequested(currentItem);
}

void ClipboardItemWidget::handleStarAction() {
    emit itemStared(currentItem);
}

void ClipboardItemWidget::updateFavoriteButton() {
    ui.actions.favoriteBtn->setIcon(QIcon(isFavorite ?
        ":/resources/resources/star_filled.svg" :
        ":/resources/resources/star_outline.svg"));
    ui.actions.favoriteBtn->setToolTip(isFavorite ?
        tr("Remove from favorites") :
        tr("Add to favorites"));

    // Update persistent indicator
    ui.favoriteIndicator->setVisible(isFavorite);
    if (isFavorite) {
        int ix = ui.innerWidget->x() + ui.innerWidget->width() - ui.favoriteIndicator->width() - 6;
        int iy = ui.innerWidget->y() + ui.innerWidget->height() - ui.favoriteIndicator->height() - 6;
        ui.favoriteIndicator->move(ix, iy);
        ui.favoriteIndicator->raise();
    }
}

void ClipboardItemWidget::showItem(const ClipboardItem& item) {
    currentItem = item;
    ui.innerWidget->showItem(currentItem);
}

void ClipboardItemWidget::setSelected(bool selected) {
    ui.innerWidget->showBorder(selected);
}

const ClipboardItem& ClipboardItemWidget::getItem() const {
    return currentItem;
}

void ClipboardItemWidget::setShortcutInfo(int num) {
    ui.innerWidget->setShortkeyInfo(num);
}

void ClipboardItemWidget::clearShortcutInfo() {
    ui.innerWidget->clearShortkeyInfo();
}

// Event handlers
void ClipboardItemWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QWidget::mousePressEvent(event);
}

void ClipboardItemWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ClipboardItemWidget::contextMenuEvent(QContextMenuEvent* event) {
    showContextMenu(event->pos());
}

void ClipboardItemWidget::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);

    // Position buttons at the top center, relative to innerWidget
    ui.actions.container->adjustSize();
    const int x = (width() - ui.actions.container->width()) / 2;
    const int y = ui.innerWidget->y() + 4;
    ui.actions.container->move(x, y);
    ui.actions.container->show();
    ui.actions.container->raise();

    // Fade in animation
    auto* opacityEffect = qobject_cast<QGraphicsOpacityEffect*>(ui.actions.container->graphicsEffect());
    auto* animation = new QPropertyAnimation(opacityEffect, "opacity", this);
    animation->setDuration(50);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ClipboardItemWidget::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);

    // Fade out animation
    auto* opacityEffect = qobject_cast<QGraphicsOpacityEffect*>(ui.actions.container->graphicsEffect());
    auto* animation = new QPropertyAnimation(opacityEffect, "opacity", this);
    animation->setDuration(50);
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    connect(animation, &QPropertyAnimation::finished,
            ui.actions.container, &QWidget::hide);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ClipboardItemWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);

    // Reposition favorite indicator after layout settles
    if (isFavorite && ui.favoriteIndicator) {
        int ix = ui.innerWidget->x() + ui.innerWidget->width() - ui.favoriteIndicator->width() - 6;
        int iy = ui.innerWidget->y() + ui.innerWidget->height() - ui.favoriteIndicator->height() - 6;
        ui.favoriteIndicator->move(ix, iy);
        ui.favoriteIndicator->raise();
    }
}
