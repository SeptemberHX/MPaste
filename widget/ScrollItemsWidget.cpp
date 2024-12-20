#include <QDir>
#include <utils/MPasteSettings.h>
#include <iostream>
#include "ScrollItemsWidget.h"
#include "ui_ScrollItemsWidget.h"
#include "ClipboardItemWidget.h"
#include <QScrollBar>
#include <QScroller>

ScrollItemsWidget::ScrollItemsWidget(const QString &category, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ScrollItemsWidget),
    category(category),
    currItemWidget(nullptr)
{
    ui->setupUi(this);

    // Enable smooth scrolling using QScroller
    QScroller::grabGesture(ui->scrollArea->viewport(), QScroller::TouchGesture);
    QScrollerProperties sp;
    sp.setScrollMetric(QScrollerProperties::FrameRate, QVariant(120));
    sp.setScrollMetric(QScrollerProperties::DragStartDistance, QVariant(100));
    QScroller::scroller(ui->scrollArea->viewport())->setScrollerProperties(sp);

    // Enable pixel scrolling
    ui->scrollArea->horizontalScrollBar()->setSingleStep(50);

    this->layout = new QHBoxLayout(ui->scrollAreaWidgetContents);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->addStretch(1);
    this->saver = new LocalSaver();
    ui->scrollArea->installEventFilter(this);
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
    connect(itemWidget, &ClipboardItemWidget::itemNeedToSave, this, [this] () {
       auto itemWidget = dynamic_cast<ClipboardItemWidget*>(sender());
       this->saveItem(itemWidget->getItem());
    });
    itemWidget->installEventFilter(this);
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

    Q_EMIT itemCountChanged(this->layout->count() - 1);
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
    Q_EMIT doubleClicked(widget->getItem());
}

void ScrollItemsWidget::filterByKeyword(const QString &keyword) {
    int visibleCount = 0;
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        widget->setVisible(widget->getItem().contains(keyword));
        if (widget->isVisible()) {
            ++visibleCount;
        }
    }
    this->setFirstVisibleItemSelected();

    Q_EMIT itemCountChanged(visibleCount);
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
    return true;
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

const ClipboardItem& ScrollItemsWidget::selectedByShortcut(int keyIndex) {
    if (keyIndex >= 0 && keyIndex < this->layout->count() - 1) {
        for (int i = 0, c = 0; i < this->layout->count() - 1; ++i) {
            auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
            if (widget->isVisible()) {
                if (c == keyIndex) {
                    this->moveItemToFirst(widget);
                    return widget->getItem();
                } else {
                    ++c;
                }
            }
        }
    }
    return this->currItemWidget->getItem();
}

QString ScrollItemsWidget::saveDir() {
    return QDir::cleanPath(MPasteSettings::getInst()->getSaveDir() + QDir::separator() + this->category);
}

void ScrollItemsWidget::focusMoveLeft() {
    if (this->currItemWidget == nullptr) {
        this->setFirstVisibleItemSelected();
    } else {
        int index = this->layout->indexOf(this->currItemWidget);
        if (index > 0 && index <= this->layout->count() - 2) {
            auto nextWidget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(index - 1)->widget());
            this->setSelectedItem(nextWidget);
            int nextWidgetLeft = nextWidget->pos().x();
            if (nextWidgetLeft < ui->scrollArea->horizontalScrollBar()->value()
                    || nextWidgetLeft > ui->scrollArea->horizontalScrollBar()->value() + ui->scrollArea->width()) {
                ui->scrollArea->horizontalScrollBar()->setValue(nextWidgetLeft);
            }
        }
    }
}

void ScrollItemsWidget::focusMoveRight() {
    if (this->currItemWidget == nullptr) {
        this->setFirstVisibleItemSelected();
    } else {
        int index = this->layout->indexOf(this->currItemWidget);
        if (index >= 0 && index < this->layout->count() - 2) {
            auto nextWidget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(index + 1)->widget());
            this->setSelectedItem(nextWidget);
            int currValue = ui->scrollArea->horizontalScrollBar()->value() + ui->scrollArea->width();
            int nextWidgetRight = nextWidget->pos().x() + nextWidget->width();
            if (nextWidgetRight > currValue || nextWidgetRight < ui->scrollArea->horizontalScrollBar()->value()) {
                ui->scrollArea->horizontalScrollBar()->setValue(nextWidgetRight - ui->scrollArea->width());
            }
        }
    }
}

void ScrollItemsWidget::scrollToFirst() {
    if (this->layout->count() <= 1) return;  // 没有条目时直接返回

    // 找到第一个可见的条目
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->isVisible()) {
            this->setSelectedItem(widget);
            ui->scrollArea->horizontalScrollBar()->setValue(0);
            break;
        }
    }
}

void ScrollItemsWidget::scrollToLast() {
    if (this->layout->count() <= 1) return;  // 没有条目时直接返回

    // 找到最后一个可见的条目
    for (int i = this->layout->count() - 2; i >= 0; --i) {
        auto widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->isVisible()) {
            this->setSelectedItem(widget);
            int maxScroll = ui->scrollArea->horizontalScrollBar()->maximum();
            ui->scrollArea->horizontalScrollBar()->setValue(maxScroll);
            break;
        }
    }
}

void ScrollItemsWidget::selectedByEnter() {
    if (this->currItemWidget != nullptr) {
        this->moveItemToFirst(this->currItemWidget);
    }
}

bool ScrollItemsWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        QGuiApplication::sendEvent(this->parent(), event);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
