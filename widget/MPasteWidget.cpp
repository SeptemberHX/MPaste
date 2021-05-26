#include <iostream>
#include <QScrollBar>
#include <QClipboard>
#include <QKeyEvent>
#include <QDir>
#include "utils/MPasteSettings.h"
#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "ClipboardItemWidget.h"

MPasteWidget::MPasteWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MPasteWidget),
    mimeData(nullptr)
{
    ui->setupUi(this);
    this->searchHideAnim = new QPropertyAnimation(ui->searchEdit, "maximumWidth");
    this->searchHideAnim->setEndValue(0);
    this->searchHideAnim->setDuration(150);
    connect(this->searchHideAnim, &QPropertyAnimation::finished, ui->searchEdit, &QLineEdit::hide);

    this->searchShowAnim = new QPropertyAnimation(ui->searchEdit, "maximumWidth");
    this->searchShowAnim->setEndValue(200);
    this->searchShowAnim->setDuration(150);

    this->aboutWidget = new AboutWidget(this);
    this->aboutWidget->setWindowFlag(Qt::Dialog);
    this->aboutWidget->setWindowTitle("MPaste About");
    this->aboutWidget->hide();

    this->clipboardWidget = new ScrollItemsWidget("Clipboard", this);
    this->clipboardWidget->installEventFilter(this);
    connect(this->clipboardWidget, &ScrollItemsWidget::updateClipboard, this, &MPasteWidget::setClipboard);
    connect(this->clipboardWidget, &ScrollItemsWidget::doubleClicked, this, &MPasteWidget::hide);

    this->numKeyList << Qt::Key_1 << Qt::Key_2 << Qt::Key_3 << Qt::Key_4 << Qt::Key_5 << Qt::Key_6 << Qt::Key_7 << Qt::Key_8 << Qt::Key_9 << Qt::Key_0;

    std::cout << "Init media player..." << std::endl;
    this->player = new QMediaPlayer(this, QMediaPlayer::LowLatency);
    this->player->setMedia(QUrl("qrc:/resources/resources/sound.mp3"));
    std::cout << "Sound effect loaded finished" << std::endl;

    this->layout = new QHBoxLayout(ui->itemsWidget);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->addWidget(this->clipboardWidget);

    this->setWindowFlags(Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::Tool | Qt::FramelessWindowHint);
    this->setObjectName("pasteWidget");
    this->setStyleSheet("QWidget#pasteWidget, #scrollAreaWidgetContents {background-color: #e6e5e4;}");

    this->monitor = new ClipboardMonitor();
    connect(this->monitor, &ClipboardMonitor::clipboardUpdated, this, &MPasteWidget::clipboardUpdated);

    ui->searchEdit->installEventFilter(this);

    this->menu = new QMenu(this);
    this->menu->addAction(tr("Settings"), [this]() { });
    this->menu->addAction(tr("About"), [this]() { this->aboutWidget->show(); });
    this->menu->addAction(tr("Quit"), [this]() { this->close(); });

    connect(ui->menuButton, &QToolButton::clicked, this, [this]() {
        this->menu->popup(ui->menuButton->mapToGlobal(ui->menuButton->rect().bottomLeft()));
    });

    connect(ui->searchEdit, &QLineEdit::textChanged, this, [this] (const QString &str) {
        this->currItemsWidget()->filterByKeyword(str);
    });
    connect(ui->searchButton, &QToolButton::clicked, this, [this] (bool flag) {
        if (flag) {
            this->setFocusOnSearch(true);
        } else {
            this->setFocusOnSearch(false);
        }
    });

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
        QCoreApplication::sendEvent(this->currItemsWidget()->horizontalScrollbar(), event);
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

void MPasteWidget::clipboardUpdated(ClipboardItem nItem, int wId) {
    this->clipboardWidget->addAndSaveItem(nItem);
    this->player->play();
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
    if (item.getImage().isNull() && !item.getText().isEmpty() && item.getUrls().isEmpty()) this->mimeData->setText(item.getText());

    if (!item.getImage().isNull()) this->mimeData->setImageData(item.getImage());
    else if (!item.getHtml().isEmpty()) this->mimeData->setHtml(item.getHtml());
    else if (!item.getUrls().isEmpty()) {
        bool files = true;
        foreach (const QUrl &url, item.getUrls()) {
            if (!url.isLocalFile() || !QFileInfo::exists(url.toLocalFile())) {
                files = false;
                break;
            }
        }

        if (files) {
            QByteArray nautilus("x-special/nautilus-clipboard\n");
            QByteArray byteArray("copy\n");
            foreach (const QUrl &url, item.getUrls()) {
                byteArray.append(url.toEncoded()).append('\n');
            }
            this->mimeData->setData("x-special/gnome-copied-files", byteArray);
            nautilus.append(byteArray);
            this->mimeData->setData("COMPOUND_TEXT", nautilus);
            this->mimeData->setText(nautilus);
            this->mimeData->setData("text/plain;charset=utf-8", nautilus);
        }

        this->mimeData->setUrls(item.getUrls());
    }
    else if (item.getColor().isValid()) this->mimeData->setColorData(item.getColor());
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
        this->currItemsWidget()->setShortcutInfo();
    } else if (event->key() == Qt::Key_Left) {
        this->currItemsWidget()->focusMoveLeft();
    } else if (event->key() == Qt::Key_Right) {
        this->currItemsWidget()->focusMoveRight();
    } else if (event->key() == Qt::Key_Return) {
        this->currItemsWidget()->selectedByEnter();
        this->hide();
    }

    if (event->modifiers() & Qt::AltModifier) {
        // shortcut for selecting top-10 items
        int keyOrder = this->numKeyList.indexOf(event->key());
        if (keyOrder >= 0 && keyOrder <= 9) {
            this->currItemsWidget()->selectedByShortcut(keyOrder);
            this->hide();
            this->currItemsWidget()->cleanShortCutInfo();
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
        this->currItemsWidget()->cleanShortCutInfo();
    }

    QWidget::keyReleaseEvent(event);
}

void MPasteWidget::loadFromSaveDir() {
    this->clipboardWidget->loadFromSaveDir();
}

void MPasteWidget::setFocusOnSearch(bool flag) {
    if (flag) {
        ui->searchEdit->show();
        this->searchShowAnim->start();
        ui->searchEdit->setFocus();
    } else {
        this->searchHideAnim->start();
        ui->searchEdit->clearFocus();
        this->setFocus();
    }
}

ScrollItemsWidget *MPasteWidget::currItemsWidget() {
    return this->clipboardWidget;
}

void MPasteWidget::setVisibleWithAnnimation(bool visible) {
    if (visible == this->isVisible()) return;
    if (visible) {
        this->show();
        if (!ui->searchEdit->text().isEmpty()) {
            ui->searchEdit->setFocus();
        }
    } else {
        this->hide();
        this->currItemsWidget()->cleanShortCutInfo();
    }
}
