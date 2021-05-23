#include <iostream>
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

//    ui->scrollArea->installEventFilter(this);
}

MPasteWidget::~MPasteWidget()
{
    delete ui;
}

bool MPasteWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel) {
        std::cout << "Wheel event" << std::endl;
        QWheelEvent *wheelEvent = dynamic_cast<QWheelEvent*>(event);

        QPoint numPixels = wheelEvent->pixelDelta();
        QPoint numDegrees = wheelEvent->angleDelta() / 8;

        if (!numPixels.isNull()) {
            ui->scrollArea->scroll(wheelEvent->pixelDelta().x(), 0);
            std::cout << numPixels.x() << " " << numPixels.y() << std::endl;
        } else if (!numDegrees.isNull()) {
            QPoint numSteps = numDegrees / 15;
            std::cout << numSteps.x() << " " << numSteps.y() << std::endl;
            ui->scrollArea->scroll(numSteps.y() * 60, 0);
        }


        event->accept();
    }

    return QObject::eventFilter(watched, event);
}
