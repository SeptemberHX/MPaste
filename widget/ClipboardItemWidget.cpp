#include "ClipboardItemWidget.h"

#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QToolButton>
#include <QMenu>
#include <QHBoxLayout>
#include <QMouseEvent>

#include "utils/MPasteSettings.h"

ClipboardItemWidget::ClipboardItemWidget(QString category, QColor borderColor, QWidget *parent)
    : category(category), borderColor(borderColor), QWidget(parent)
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
}

void ClipboardItemWidget::setupActionButtons() {
    // Container setup
    ui.actions.container = new QWidget(this);
    ui.actions.container->setFixedSize(50, 24);
    ui.actions.container->hide();

    ui.actions.layout = new QHBoxLayout(ui.actions.container);
    ui.actions.layout->setContentsMargins(0, 0, 0, 0);
    ui.actions.layout->setSpacing(0);

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
    auto* button = new QToolButton;
    button->setIcon(QIcon(iconPath));
    button->setIconSize(QSize(16, 16));
    button->setFixedSize(16, 16);
    button->setStyleSheet(R"(
        ClipboardItemWidget QToolButton {
            background: transparent;
            border: none;
            padding: 0px;
        }
        ClipboardItemWidget QToolButton:hover {
            background: rgba(0, 0, 0, 0.1);
            border-radius: 3px;
        }
    )");
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
}

void ClipboardItemWidget::handleDeleteAction() {
    emit deleteRequested();
}

void ClipboardItemWidget::handleSaveAction() {
    emit saveRequested(currentItem);
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

    // Position buttons at the top center
    const int x = (width() - ui.actions.container->width()) / 2;
    ui.actions.container->move(x, 4);
    ui.actions.container->show();

    // Fade in animation
    auto* animation = new QPropertyAnimation(ui.actions.container, "opacity", this);
    animation->setDuration(50);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ClipboardItemWidget::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);

    // Fade out animation
    auto* animation = new QPropertyAnimation(ui.actions.container, "opacity", this);
    animation->setDuration(50);
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    connect(animation, &QPropertyAnimation::finished, 
            ui.actions.container, &QWidget::hide);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}
