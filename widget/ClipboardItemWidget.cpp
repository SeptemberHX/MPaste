//
// Created by ragdoll on 2021/5/22.
//

#include <QGraphicsDropShadowEffect>
#include "ClipboardItemWidget.h"
#include "XUtils.h"
#include "ClipboardMonitor.h"

ClipboardItemWidget::ClipboardItemWidget(QWidget *parent)
    : QWidget(parent)
{
    this->layout = new QHBoxLayout(this);

    this->innerShadowedWidget = new ClipboardItemInnerWidget(this);
    this->innerShadowedWidget->setObjectName("innerWidget");
    this->layout->addWidget(this->innerShadowedWidget);

//    this->setWindowFlags(Qt::FramelessWindowHint);
    this->setAttribute(Qt::WA_TranslucentBackground);

    auto *effect = new QGraphicsDropShadowEffect(this);
    effect->setOffset(0, 0);
    effect->setColor(Qt::black);
    effect->setBlurRadius(20);
    this->innerShadowedWidget->setGraphicsEffect(effect);
    this->innerShadowedWidget->setAttribute(Qt::WA_TranslucentBackground, false);
}

void ClipboardItemWidget::showItem(ClipboardItem nItem) {
    this->item = nItem;
    this->innerShadowedWidget->showItem(this->item);
}
