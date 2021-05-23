#include <iostream>
#include <QScrollBar>
#include <QClipboard>
#include <QKeyEvent>
#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "ClipboardItemWidget.h"

MPasteWidget::MPasteWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MPasteWidget),
    currItemWidget(nullptr),
    mimeData(nullptr)
{
    ui->setupUi(this);
    this->layout = new QHBoxLayout(ui->scrollAreaWidgetContents);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->addStretch(1);

    this->setWindowFlags(Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::Dialog | Qt::FramelessWindowHint);
    this->setObjectName("pasteWidget");
    this->setStyleSheet("QWidget#pasteWidget, #scrollAreaWidgetContents {background-color: #e6e5e4;}");

    this->monitor = new ClipboardMonitor();
    connect(this->monitor, &ClipboardMonitor::clipboardUpdated, this, &MPasteWidget::clipboardUpdated);
    this->monitor->clipboardChanged();

    ui->scrollArea->installEventFilter(this);

    this->menu = new QMenu(this);
    this->menu->addAction(tr("Settings"), [this]() { });
    this->menu->addAction(tr("About"), [this]() { });
    this->menu->addAction(tr("Quit"), [this]() { this->close(); });

    connect(ui->menuButton, &QToolButton::clicked, this, [this]() {
        this->menu->popup(ui->menuButton->mapToGlobal(ui->menuButton->rect().bottomLeft()));
    });
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

void MPasteWidget::itemClicked() {
    ClipboardItemWidget *widget = dynamic_cast<ClipboardItemWidget*>(sender());
    this->setSelectedItem(widget);
}

void MPasteWidget::setCurrentItem(ClipboardItemWidget *widget) {
    this->setSelectedItem(widget);
    this->layout->removeWidget(widget);
    this->layout->insertWidget(0, widget, 0, Qt::AlignLeft);
}

void MPasteWidget::itemDoubleClicked() {
    ClipboardItemWidget *widget = dynamic_cast<ClipboardItemWidget*>(sender());
    this->hide();
    this->setCurrentItem(widget);
    this->setClipboard(widget->getItem());
}

void MPasteWidget::clipboardUpdated(ClipboardItem nItem, int wId) {
    if (nItem.isEmpty()) return;

    for (int i = 0; i < this->layout->count() - 1; ++i) {
        ClipboardItemWidget *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget->getItem().sameContent(nItem)) {
            this->setCurrentItem(widget);
            return;
        }
    }

    auto itemWidget = new ClipboardItemWidget(ui->scrollAreaWidgetContents);
    connect(itemWidget, &ClipboardItemWidget::clicked, this, &MPasteWidget::itemClicked);
    connect(itemWidget, &ClipboardItemWidget::doubleClicked, this, &MPasteWidget::itemDoubleClicked);

    itemWidget->showItem(nItem);
    this->setCurrentItem(itemWidget);
}

void MPasteWidget::setSelectedItem(ClipboardItemWidget *widget) {
    if (this->currItemWidget != nullptr) {
        this->currItemWidget->setSelected(false);
    }
    this->currItemWidget = widget;
    widget->setSelected(true);
}

void MPasteWidget::setClipboard(const ClipboardItem &item) {
    if (this->mimeData != nullptr) {
        // todo: we should delete it here. however, sometimes it causes crash. I suspect the system deletes it before here.
//        try {
//            delete this->mimeData;
//        } catch (...) {
//            std::cout << " ++++++ " << std::endl;
//        }
    }

    this->mimeData = new QMimeData();
    this->mimeData->setText(item.getText());
    this->mimeData->setHtml(item.getHtml());
    this->mimeData->setUrls(item.getUrls());
    this->mimeData->setImageData(item.getImage());
    QGuiApplication::clipboard()->setMimeData(this->mimeData);
}

void MPasteWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        this->hide();
    }

    QWidget::keyPressEvent(event);
}
