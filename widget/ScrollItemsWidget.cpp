#include <QDir>
#include <MPasteSettings.h>
#include <iostream>
#include "ScrollItemsWidget.h"
#include "ui_ScrollItemsWidget.h"
#include "ClipboardItemWidget.h"

ScrollItemsWidget::ScrollItemsWidget(const QString &category, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ScrollItemsWidget),
    category(category),
    currItemWidget(nullptr)
{
    ui->setupUi(this);

    this->layout = new QHBoxLayout(ui->scrollAreaWidgetContents);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->addStretch(1);
    this->saver = new LocalSaver();
}

ScrollItemsWidget::~ScrollItemsWidget()
{
    delete ui;
}

bool ScrollItemsWidget::addOneItem(const ClipboardItem &nItem) {
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
    connect(itemWidget, &ClipboardItemWidget::clicked, this, &ScrollItemsWidget::itemClicked);
    connect(itemWidget, &ClipboardItemWidget::doubleClicked, this, &ScrollItemsWidget::itemDoubleClicked);
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

void ScrollItemsWidget::setSelectedItem(ClipboardItemWidget *widget) {
    if (this->currItemWidget != nullptr) {
        this->currItemWidget->setSelected(false);
    }
    this->currItemWidget = widget;
    widget->setSelected(true);
}

QString ScrollItemsWidget::getItemFilePath(const ClipboardItem &item) {
    return QDir::cleanPath(this->saveDir() + QDir::separator() + item.getName() + ".mpaste");
}

void ScrollItemsWidget::setFirstVisibleItemSelected() {
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

void ScrollItemsWidget::moveItemToFirst(ClipboardItemWidget *widget) {
    ClipboardItem item(widget->getItem().getIcon(),
                       widget->getItem().getText(),
                       widget->getItem().getImage(),
                       widget->getItem().getHtml(),
                       widget->getItem().getUrls(),
                       widget->getItem().getColor());
    this->saver->removeItem(this->getItemFilePath(widget->getItem()));
    this->saveItem(item);
    this->layout->removeWidget(widget);
    this->layout->insertWidget(0, widget);
    this->setSelectedItem(widget);
    widget->showItem(item);

    Q_EMIT updateClipboard(item);
}

void ScrollItemsWidget::saveItem(const ClipboardItem &item) {
    this->checkSaveDir();
    this->saver->saveToFile(item, this->getItemFilePath(item));
}

void ScrollItemsWidget::checkSaveDir() {
    QDir dir;
    QString path = QDir::cleanPath(this->saveDir());
    if (!dir.exists(path)) {
        dir.mkpath(path);
    }
}

void ScrollItemsWidget::itemClicked() {
    ClipboardItemWidget *widget = dynamic_cast<ClipboardItemWidget*>(sender());
    this->setSelectedItem(widget);
}

void ScrollItemsWidget::itemDoubleClicked() {
    auto *widget = dynamic_cast<ClipboardItemWidget*>(sender());
    this->moveItemToFirst(widget);
    Q_EMIT doubleClicked();
}

void ScrollItemsWidget::filterByKeyword(const QString &keyword) {
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        widget->setVisible(widget->getItem().contains(keyword));
    }
    this->setFirstVisibleItemSelected();
}

void ScrollItemsWidget::setShortcutInfo() {
    // set shortcut information for selecting top-10 items
    for (int i = 0, c = 0; i < this->layout->count() - 1 && c < 10; ++i) {
        auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget->isVisible()) {
            widget->setShortcutInfo((c + 1) % 10);
            ++c;
        }
    }
}

void ScrollItemsWidget::cleanShortCutInfo() {
    // remove the shortcut information after releasing Alt key
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        widget->clearShortcutInfo();
    }
}

void ScrollItemsWidget::loadFromSaveDir() {
    this->checkSaveDir();

    QDir saveDir(this->saveDir());
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

QScrollBar* ScrollItemsWidget::horizontalScrollbar() {
    return ui->scrollArea->horizontalScrollBar();
}

bool ScrollItemsWidget::addAndSaveItem(const ClipboardItem &nItem) {
    if (this->addOneItem(nItem)) {
        this->saveItem(nItem);
    }
}

void ScrollItemsWidget::setAllItemVisible() {
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        widget->setVisible(true);
    }
    if (this->layout->count() > 1) {
        this->setSelectedItem(dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(0)->widget()));
    }
}

void ScrollItemsWidget::selectedByShortcut(int keyIndex) {
    if (keyIndex >= 0 && keyIndex < this->layout->count() - 1) {
        for (int i = 0, c = 0; i < this->layout->count() - 1; ++i) {
            auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
            if (widget->isVisible()) {
                if (c == keyIndex) {
                    this->moveItemToFirst(widget);
                    break;
                } else {
                    ++c;
                }
            }
        }
    }
}

QString ScrollItemsWidget::saveDir() {
    return QDir::cleanPath(MPasteSettings::getInst()->getSaveDir() + QDir::separator() + this->category);
}