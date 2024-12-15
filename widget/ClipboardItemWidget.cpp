//
// Created by ragdoll on 2021/5/22.
//

#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include "ClipboardItemWidget.h"
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
    effect->setOffset(0, 0);
    effect->setColor(Qt::darkGray);
    effect->setBlurRadius(15);
    this->innerShadowedWidget->setGraphicsEffect(effect);
    this->innerShadowedWidget->setAttribute(Qt::WA_TranslucentBackground, false);
}

void ClipboardItemWidget::showItem(ClipboardItem nItem) {
    this->item = nItem;
    this->innerShadowedWidget->showItem(this->item);
}

void ClipboardItemWidget::setSelected(bool flag) {
    this->innerShadowedWidget->showBorder(flag);
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

const ClipboardItem &ClipboardItemWidget::getItem() const {
    return item;
}

void ClipboardItemWidget::setShortcutInfo(int num) {
    this->innerShadowedWidget->setShortkeyInfo(num);
}

void ClipboardItemWidget::clearShortcutInfo() {
    this->innerShadowedWidget->clearShortkeyInfo();
}
