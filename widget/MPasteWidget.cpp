#include <iostream>
#include <QScrollBar>
#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "ClipboardItemWidget.h"

MPasteWidget::MPasteWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MPasteWidget)
{
    ui->setupUi(this);
    this->layout = new QHBoxLayout(ui->scrollAreaWidgetContents);
    this->layout->setContentsMargins(0, 0, 0, 0);
//    this->setWindowFlags(Qt::FramelessWindowHint);

    this->monitor = new ClipboardMonitor();
    this->monitor = new ClipboardMonitor();
    connect(this->monitor, &ClipboardMonitor::clipboardUpdated, this, [this] (ClipboardItem nItem, int wId) {
        ClipboardItemWidget *itemWidget = new ClipboardItemWidget(ui->scrollAreaWidgetContents);
        this->layout->insertWidget(0, itemWidget);
        this->layout->addStretch();
        itemWidget->showItem(nItem);
    });

    ui->scrollArea->installEventFilter(this);
}

MPasteWidget::~MPasteWidget()
{
    delete ui;
}

bool MPasteWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel) {
        QCoreApplication::sendEvent(ui->scrollArea->horizontalScrollBar(), event);
    }

    return QObject::eventFilter(watched, event);
}
