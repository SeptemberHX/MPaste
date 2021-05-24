#include <iostream>
#include <QScrollBar>
#include <QClipboard>
#include <QKeyEvent>
#include <QDir>
#include "MPasteSettings.h"
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
    this->aboutWidget = new AboutWidget(this);
    this->aboutWidget->setWindowFlag(Qt::Dialog);
    this->aboutWidget->setWindowTitle("MPaste About");
    this->aboutWidget->hide();

    this->saver = new LocalSaver();
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

    ui->scrollArea->installEventFilter(this);
    ui->searchEdit->installEventFilter(this);

    this->menu = new QMenu(this);
    this->menu->addAction(tr("Settings"), [this]() { });
    this->menu->addAction(tr("About"), [this]() { this->aboutWidget->show(); });
    this->menu->addAction(tr("Quit"), [this]() { this->close(); });

    connect(ui->menuButton, &QToolButton::clicked, this, [this]() {
        this->menu->popup(ui->menuButton->mapToGlobal(ui->menuButton->rect().bottomLeft()));
    });

    connect(ui->searchEdit, &QLineEdit::textChanged, this, &MPasteWidget::filterByKeyword);
    connect(ui->searchButton, &QToolButton::clicked, this, [this] () { this->setFocusOnSearch(true); } );

    this->loadFromSaveDir();
    this->monitor->clipboardChanged();
    this->setFocusOnSearch(false);
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
    } else if (event->type() == QEvent::KeyPress) {
        // make sure Alt+num still works even current focus is on the search area
        if (watched == ui->searchEdit) {
            auto keyEvent = dynamic_cast<QKeyEvent*>(event);
            if (keyEvent->modifiers() & Qt::AltModifier) {
                QGuiApplication::sendEvent(this, keyEvent);
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void MPasteWidget::itemClicked() {
    ClipboardItemWidget *widget = dynamic_cast<ClipboardItemWidget*>(sender());
    this->setSelectedItem(widget);
}

void MPasteWidget::itemDoubleClicked() {
    auto *widget = dynamic_cast<ClipboardItemWidget*>(sender());
    this->moveItemToFirst(widget);
    this->hide();
}

void MPasteWidget::clipboardUpdated(ClipboardItem nItem, int wId) {
    if (this->addOneItem(nItem)) {
        this->saveItem(nItem);
    }
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
    if (item.getImage().isNull() && !item.getText().isEmpty()) this->mimeData->setText(item.getText());

    if (!item.getImage().isNull()) this->mimeData->setImageData(item.getImage());
    else if (!item.getHtml().isEmpty()) this->mimeData->setHtml(item.getHtml());
    else if (!item.getUrls().isEmpty()) this->mimeData->setUrls(item.getUrls());
    QGuiApplication::clipboard()->setMimeData(this->mimeData);
}

void MPasteWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        // unfocus the search area and show all items, or hide window
        if (ui->searchEdit->hasFocus()) {
            ui->searchEdit->clear();
            this->setFocusOnSearch(false);
        } else {
            this->hide();
        }
    } else if (event->key() == Qt::Key_Alt) {
        // set shortcut information for selecting top-10 items
        for (int i = 0, c = 0; i < this->layout->count() - 1 && c < 10; ++i) {
            auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
            if (widget->isVisible()) {
                widget->setShortcutInfo((c + 1) % 10);
                ++c;
            }
        }
    }

    if (event->modifiers() & Qt::AltModifier) {
        // shortcut for selecting top-10 items
        int keyIndex = this->numKeyList.indexOf(event->key());
        if (keyIndex >= 0 && keyIndex < this->layout->count() - 1) {
            for (int i = 0, c = 0; i < this->layout->count() - 1; ++i) {
                auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
                if (widget->isVisible()) {
                    if (c == keyIndex) {
                        this->moveItemToFirst(widget);
                        this->hide();
                        break;
                    } else {
                        ++c;
                    }
                }
            }
        }
    } else if (event->key() >= Qt::Key_Space && event->key() <= Qt::Key_AsciiTilde) {
        // make sure we can get to search area by pressing any characters
        QGuiApplication::sendEvent(ui->searchEdit, event);
        this->setFocusOnSearch(true);
    }

    QWidget::keyPressEvent(event);
}

void MPasteWidget::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt) {
        // remove the shortcut information after releasing Alt key
        for (int i = 0; i < this->layout->count() - 1; ++i) {
            auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
            widget->clearShortcutInfo();
        }
    }

    QWidget::keyReleaseEvent(event);
}

void MPasteWidget::saveItem(const ClipboardItem &item) {
    this->checkSaveDir();

    this->saver->saveToFile(item, this->getItemFilePath(item));
}

void MPasteWidget::checkSaveDir() {
    QDir dir;
    if (!dir.exists(MPasteSettings::getInst()->getSaveDir())) {
        dir.mkpath(MPasteSettings::getInst()->getSaveDir());
    }
}

void MPasteWidget::loadFromSaveDir() {
    this->checkSaveDir();

    QDir saveDir(MPasteSettings::getInst()->getSaveDir());
    QList<ClipboardItem> itemList;
    foreach (QFileInfo info, saveDir.entryInfoList(QStringList() << "*.mpaste", QDir::Files)) {
        ClipboardItem item = this->saver->loadFromFile(info.filePath());
        itemList << item;
    }

    std::sort(itemList.begin(), itemList.end(), [](const ClipboardItem &item1, const ClipboardItem &item2) {
        return item1.getTime() < item2.getTime();
    });

    foreach (const ClipboardItem &item, itemList) {
        this->addOneItem(item);
    }
}

bool MPasteWidget::addOneItem(const ClipboardItem &nItem) {
    if (nItem.isEmpty()) return false;

    for (int i = 0; i < this->layout->count() - 1; ++i) {
        ClipboardItemWidget *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget->getItem().sameContent(nItem)) {
            if (i != 0) {
                this->moveItemToFirst(widget);
            }
            return false;
        }
    }

    auto itemWidget = new ClipboardItemWidget(ui->scrollAreaWidgetContents);
    connect(itemWidget, &ClipboardItemWidget::clicked, this, &MPasteWidget::itemClicked);
    connect(itemWidget, &ClipboardItemWidget::doubleClicked, this, &MPasteWidget::itemDoubleClicked);
    itemWidget->showItem(nItem);

    this->layout->insertWidget(0, itemWidget);
    this->setSelectedItem(itemWidget);

    for (int i = MPasteSettings::getInst()->getMaxSize(); i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        this->saver->removeItem(this->getItemFilePath(widget->getItem()));
        this->layout->removeWidget(widget);
        if (this->currItemWidget == widget) {
            this->currItemWidget = nullptr;
            this->setFirstVisibleItemSelected();
        }
    }

    return true;
}

void MPasteWidget::removeOneItemByWidget(ClipboardItemWidget *widget) {
    if (this->currItemWidget == widget) {
        this->currItemWidget = nullptr;
    }
    this->layout->removeWidget(widget);
    this->saver->removeItem(this->getItemFilePath(widget->getItem()));
    widget->deleteLater();
}

QString MPasteWidget::getItemFilePath(const ClipboardItem &item) {
return QDir::cleanPath(MPasteSettings::getInst()->getSaveDir() + QDir::separator() + item.getName() + ".mpaste");
}

void MPasteWidget::moveItemToFirst(ClipboardItemWidget *widget) {
    ClipboardItem item(widget->getItem().getIcon(),
                       widget->getItem().getText(),
                       widget->getItem().getImage(),
                       widget->getItem().getHtml(),
                       widget->getItem().getUrls());
    this->saver->removeItem(this->getItemFilePath(widget->getItem()));
    this->saveItem(item);
    this->layout->removeWidget(widget);
    this->layout->insertWidget(0, widget);
    this->setSelectedItem(widget);
    widget->showItem(item);
    this->setClipboard(item);
}

void MPasteWidget::filterByKeyword(const QString &keyword) {
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        widget->setVisible(widget->getItem().contains(keyword));
    }
    this->setFirstVisibleItemSelected();
}

void MPasteWidget::setAllItemVisible() {
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        widget->setVisible(true);
    }
    if (this->layout->count() > 1) {
        this->setSelectedItem(dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(0)->widget()));
    }
}

void MPasteWidget::setFocusOnSearch(bool flag) {
    if (flag) {
        ui->searchEdit->show();
        ui->searchEdit->setFocus();
    } else {
        ui->searchEdit->hide();
        ui->searchEdit->clearFocus();
        this->setFocus();
    }
}

void MPasteWidget::setFirstVisibleItemSelected() {
    int c = -1;
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (c < 0 && widget->isVisible()) {
            c = i;
            break;
        }
    }
    if (c >= 0) {
        this->setSelectedItem(dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(c)->widget()));
    }
}
