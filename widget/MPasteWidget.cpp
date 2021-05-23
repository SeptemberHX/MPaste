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
    this->numKeyList << Qt::Key_1 << Qt::Key_2 << Qt::Key_3 << Qt::Key_4 << Qt::Key_5 << Qt::Key_6 << Qt::Key_7 << Qt::Key_8 << Qt::Key_9 << Qt::Key_0;

    std::cout << "Init media player..." << std::endl;
    this->player = new QMediaPlayer(this, QMediaPlayer::LowLatency);
    this->player->setMedia(QUrl("qrc:/resources/resources/sound.mp3"));
    std::cout << "Sound effect loaded finished" << std::endl;

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

        // it seems to crash with 5.11 on UOS, but work well on Deepin V20
#if (QT_VERSION >= QT_VERSION_CHECK(5,12,0))
        QCoreApplication::sendEvent(ui->scrollArea->horizontalScrollBar(), event);
        return true;
#endif

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
    this->setCurrentItemAndClipboard(widget);
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
    this->player->play();
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

    if (event->key() == Qt::Key_Alt) {
        for (int i = 0; i < this->layout->count() - 1; ++i) {
            auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
            widget->setShortcutInfo((i + 1) % 10);
        }
    }

    if (event->modifiers() & Qt::AltModifier) {
        int keyIndex = this->numKeyList.indexOf(event->key());
        if (keyIndex >= 0 && keyIndex < this->layout->count() - 1) {
            auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(keyIndex)->widget());
            this->setCurrentItemAndClipboard(widget);
        }
    }

    QWidget::keyPressEvent(event);
}

void MPasteWidget::setCurrentItemAndClipboard(ClipboardItemWidget *widget) {
    this->setCurrentItem(widget);
    this->setClipboard(widget->getItem());
    this->hide();
}

void MPasteWidget::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt) {
        for (int i = 0; i < this->layout->count() - 1; ++i) {
            auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
            widget->clearShortcutInfo();
        }
    }

    QWidget::keyReleaseEvent(event);
}
