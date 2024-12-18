//
// Created by ragdoll on 2021/5/22.
//

#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include "ClipboardItemWidget.h"

#include <qabstractanimation.h>
#include <QPropertyAnimation>
#include <QToolButton>

#include "utils/ClipboardMonitor.h"

ClipboardItemWidget::ClipboardItemWidget(QWidget *parent)
    : QWidget(parent)
{
    this->layout = new QHBoxLayout(this);

    // Enable hardware acceleration
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);

    // Enable widget caching
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setAttribute(Qt::WA_Hover);

    // Optimize painting
    setAutoFillBackground(false);

    this->innerShadowedWidget = new ClipboardItemInnerWidget(this);
    this->innerShadowedWidget->setObjectName("innerWidget");
    connect(this->innerShadowedWidget, &ClipboardItemInnerWidget::itemNeedToSave, this, [this] (const ClipboardItem &item) {
        this->item = item;
        Q_EMIT itemNeedToSave();
    });

    this->layout->addWidget(this->innerShadowedWidget);
    this->setAttribute(Qt::WA_TranslucentBackground);

    auto *effect = new QGraphicsDropShadowEffect(this);
    effect->setOffset(0, 2);  // 添加轻微的向下偏移
    effect->setColor(QColor(0, 0, 0, 80));  // 使用半透明黑色 (alpha=80)
    effect->setBlurRadius(10);  // 稍微减小模糊半径
    this->innerShadowedWidget->setGraphicsEffect(effect);
    this->innerShadowedWidget->setAttribute(Qt::WA_TranslucentBackground, false);

    // ... 现有初始化代码 ...
    isFavorite = false;
    setupActionButtons();

    setupContextMenu();
}

void ClipboardItemWidget::showItem(ClipboardItem nItem) {
    this->item = nItem;
    this->innerShadowedWidget->showItem(this->item);
}

void ClipboardItemWidget::setSelected(bool flag) {
    this->innerShadowedWidget->showBorder(flag);
}

void ClipboardItemWidget::setupActionButtons() {
    setupButtonContainer();

    // 创建收藏按钮
    favoriteButton = createActionButton(
        ":/resources/resources/star_outline.svg",
        tr("Add to favorites")
    );

    // 创建删除按钮
    deleteButton = createActionButton(
        ":/resources/resources/delete.svg",
        tr("Delete")
    );

    // 将按钮添加到容器中
    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(buttonContainer->layout());
    layout->addWidget(favoriteButton);
    layout->addWidget(deleteButton);

    // 连接信号
    connect(favoriteButton, &QToolButton::clicked, this, &ClipboardItemWidget::onFavoriteClicked);
    connect(deleteButton, &QToolButton::clicked, this, &ClipboardItemWidget::onDeleteClicked);
}

void ClipboardItemWidget::setupButtonContainer() {
    buttonContainer = new QWidget(this);
    buttonContainer->setFixedHeight(24);
    buttonContainer->setFixedWidth(50);

    // 使用水平布局来排列按钮
    buttonLayout = new QHBoxLayout(buttonContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);  // 按钮之间的间距

    // 默认隐藏按钮容器
    buttonContainer->hide();
}

QToolButton * ClipboardItemWidget::createActionButton(const QString &iconPath, const QString &tooltip) {
    QToolButton* button = new QToolButton;
    button->setIcon(QIcon(iconPath));
    button->setIconSize(QSize(16, 16));
    button->setFixedSize(16, 16);
    button->setStyleSheet(R"(
        QToolButton {
            background: transparent;
            border: none;
            padding: 0px;
        }
        QToolButton:hover {
            background: rgba(0, 0, 0, 0.1);
            border-radius: 3px;
        }
    )");
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);

    return button;
}

void ClipboardItemWidget::setupContextMenu() {
    contextMenu = new QMenu(this);

    // 添加菜单项
    QAction* saveAction = new QAction(QIcon(":/resources/resources/save_black.svg"), tr("Save"), this);
    QAction* addToAction = new QAction(QIcon(":/resources/resources/add_black.svg"), tr("Add to"), this);
    QAction* deleteAction = new QAction(QIcon(":/resources/resources/delete.svg"), tr("Delete"), this);

    contextMenu->addAction(saveAction);
    contextMenu->addAction(addToAction);
    contextMenu->addSeparator();
    contextMenu->addAction(deleteAction);

    // 连接信号槽
    connect(saveAction, &QAction::triggered, this, &ClipboardItemWidget::onSaveTriggered);
    connect(addToAction, &QAction::triggered, this, &ClipboardItemWidget::onFavoriteClicked);
    connect(deleteAction, &QAction::triggered, this, &ClipboardItemWidget::onDeleteClicked);
}

void ClipboardItemWidget::showContextMenu(const QPoint &pos) {
    // 在点击位置显示菜单
    contextMenu->exec(mapToGlobal(pos));
}

void ClipboardItemWidget::onFavoriteClicked() {
    isFavorite = !isFavorite;

    // 更新按钮图标
    favoriteButton->setIcon(QIcon(isFavorite ?
        ":/resources/resources/star_filled.svg" :
        ":/resources/resources/star_outline.svg"));
    favoriteButton->setToolTip(isFavorite ? tr("Remove from favorites") : tr("Add to favorites"));

    emit favoriteChanged(isFavorite);
}

void ClipboardItemWidget::onDeleteClicked() {
    emit deleteRequested();
}

void ClipboardItemWidget::onSaveTriggered() {
    emit saveRequested(this->item);
}

void ClipboardItemWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        Q_EMIT clicked();
    }

    QWidget::mousePressEvent(event);
}

void ClipboardItemWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        Q_EMIT doubleClicked();
    }

    QWidget::mouseDoubleClickEvent(event);
}

void ClipboardItemWidget::contextMenuEvent(QContextMenuEvent *event) {
    showContextMenu(event->pos());
}

void ClipboardItemWidget::enterEvent(QEnterEvent *event) {
    QWidget::enterEvent(event);

    // 计算按钮位置 (顶部居中)
    int x = (width() - buttonContainer->width()) / 2;
    buttonContainer->move(x, 0);  // 让按钮垂直方向上半隐藏
    buttonContainer->show();

    // 使用渐变动画显示按钮
    QPropertyAnimation *animation = new QPropertyAnimation(buttonContainer, "opacity");
    animation->setDuration(50);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ClipboardItemWidget::leaveEvent(QEvent *event) {
    QWidget::leaveEvent(event);

    // 使用渐变动画隐藏按钮
    QPropertyAnimation *animation = new QPropertyAnimation(buttonContainer, "opacity");
    animation->setDuration(50);
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    connect(animation, &QPropertyAnimation::finished, buttonContainer, &QToolButton::hide);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

const ClipboardItem &ClipboardItemWidget::getItem() const {
    return item;
}

void ClipboardItemWidget::setShortcutInfo(int num) {
    this->innerShadowedWidget->setShortkeyInfo(num);
}

void ClipboardItemWidget::clearShortcutInfo() {
    this->innerShadowedWidget->clearShortkeyInfo();
}
