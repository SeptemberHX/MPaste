#include <QDir>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QScroller>
#include <QWheelEvent>

#include <utils/MPasteSettings.h>

#include "ScrollItemsWidget.h"
#include "ui_ScrollItemsWidget.h"
#include "ClipboardItemWidget.h"

ScrollItemsWidget::ScrollItemsWidget(const QString &category, const QString &borderColor, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ScrollItemsWidget),
    layout(nullptr),
    category(category),
    borderColor(borderColor),
    currItemWidget(nullptr),
    saver(nullptr),
    scrollAnimation(nullptr)
{
    ui->setupUi(this);

    int scale = MPasteSettings::getInst()->getItemScale();
    int scrollHeight = 300 * scale / 100 + 20;
    ui->scrollArea->setMinimumHeight(scrollHeight);
    ui->scrollArea->setMaximumHeight(scrollHeight + 20);
    setFixedHeight(scrollHeight + 20);

    QScroller::grabGesture(ui->scrollArea->viewport(), QScroller::TouchGesture);
    QScroller *scroller = QScroller::scroller(ui->scrollArea->viewport());
    QScrollerProperties sp = scroller->scrollerProperties();
    sp.setScrollMetric(QScrollerProperties::FrameRate, QVariant::fromValue(QScrollerProperties::Fps60));
    sp.setScrollMetric(QScrollerProperties::DragStartDistance, 0.0025);
    scroller->setScrollerProperties(sp);

    ui->scrollArea->horizontalScrollBar()->setSingleStep(48);
    ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    scrollAnimation = new QPropertyAnimation(ui->scrollArea->horizontalScrollBar(), "value", this);
    scrollAnimation->setEasingCurve(QEasingCurve::OutQuad);
    scrollAnimation->setDuration(70);

    this->layout = new QHBoxLayout(ui->scrollAreaWidgetContents);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->addStretch(1);
    this->saver = new LocalSaver();

    ui->scrollArea->installEventFilter(this);
    ui->scrollArea->viewport()->installEventFilter(this);
    connect(ui->scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) { maybeLoadMoreItems(); });
}

ScrollItemsWidget::~ScrollItemsWidget()
{
    delete ui;
}

ClipboardItemWidget *ScrollItemsWidget::createItemWidget(const ClipboardItem &item) {
    auto *itemWidget = new ClipboardItemWidget(this->category, QColor::fromString(this->borderColor), ui->scrollAreaWidgetContents);
    connect(itemWidget, &ClipboardItemWidget::clicked, this, &ScrollItemsWidget::itemClicked);
    connect(itemWidget, &ClipboardItemWidget::doubleClicked, this, &ScrollItemsWidget::itemDoubleClicked);
    connect(itemWidget, &ClipboardItemWidget::itemNeedToSave, this, [this] () {
        auto *itemWidget = dynamic_cast<ClipboardItemWidget*>(sender());
        if (itemWidget) {
            this->saveItem(itemWidget->getItem());
        }
    });
    connect(itemWidget, &ClipboardItemWidget::deleteRequested, this, [this] () {
        auto *itemWidget = dynamic_cast<ClipboardItemWidget*>(sender());
        if (!itemWidget) {
            return;
        }

        this->saver->removeItem(this->getItemFilePath(itemWidget->getItem()));
        this->layout->removeWidget(itemWidget);
        if (this->currItemWidget == itemWidget) {
            this->currItemWidget = nullptr;
        }
        itemWidget->deleteLater();
        this->layout->update();

        if (this->totalItemCount_ > 0) {
            --this->totalItemCount_;
        }
        if (!this->currItemWidget) {
            this->setFirstVisibleItemSelected();
        }
        this->maybeLoadMoreItems();

        emit itemCountChanged(this->itemCountForDisplay());
    });
    connect(itemWidget, &ClipboardItemWidget::itemStared, this, &ScrollItemsWidget::itemStared);
    connect(itemWidget, &ClipboardItemWidget::itemUnstared, this, &ScrollItemsWidget::itemUnstared);

    itemWidget->installEventFilter(this);
    itemWidget->showItem(item);

    if (this->category == MPasteSettings::STAR_CATEGORY_NAME) {
        itemWidget->setFavorite(true);
    }

    return itemWidget;
}

bool ScrollItemsWidget::addOneItem(const ClipboardItem &nItem) {
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (!widget) {
            continue;
        }

        if (widget->getItem() == nItem) {
            if (nItem.getMimeData() && widget->getItem().getMimeData()
                && nItem.getMimeData()->hasHtml() && widget->getItem().getMimeData()->hasHtml()
                && nItem.getMimeData()->html().length() > widget->getItem().getMimeData()->html().length()) {
                this->saver->removeItem(this->getItemFilePath(widget->getItem()));
                widget->showItem(nItem);
                this->saveItem(nItem);
            }
            if (i != 0) {
                this->moveItemToFirst(widget);
            }
            return false;
        }
    }

    auto *itemWidget = createItemWidget(nItem);
    this->layout->insertWidget(0, itemWidget);
    this->setSelectedItem(itemWidget);

    ++this->totalItemCount_;
    this->trimToMaxSize();
    if (!this->currItemWidget) {
        this->setFirstVisibleItemSelected();
    }

    emit itemCountChanged(this->itemCountForDisplay());
    return true;
}

void ScrollItemsWidget::setSelectedItem(ClipboardItemWidget *widget) {
    if (widget == nullptr || widget == this->currItemWidget) {
        return;
    }

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
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->isVisible()) {
            this->setSelectedItem(widget);
            return;
        }
    }
    this->currItemWidget = nullptr;
}

void ScrollItemsWidget::moveItemToFirst(ClipboardItemWidget *widget) {
    if (!widget) {
        return;
    }

    ClipboardItem item(widget->getItem());
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
    auto *widget = dynamic_cast<ClipboardItemWidget*>(sender());
    this->setSelectedItem(widget);
}

void ScrollItemsWidget::itemDoubleClicked() {
    auto *widget = dynamic_cast<ClipboardItemWidget*>(sender());
    if (!widget) {
        return;
    }
    this->moveItemToFirst(widget);
    emit doubleClicked(widget->getItem());
}

void ScrollItemsWidget::filterByKeyword(const QString &keyword) {
    currentKeyword_ = keyword;
    applyFilters();
}

void ScrollItemsWidget::filterByType(ClipboardItem::ContentType type) {
    currentTypeFilter_ = type;
    applyFilters();
}

void ScrollItemsWidget::applyFilters() {
    if ((!currentKeyword_.isEmpty() || currentTypeFilter_ != ClipboardItem::All) && !pendingLoadFilePaths_.isEmpty()) {
        ensureAllItemsLoaded();
    }

    int visibleCount = 0;
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (!widget) {
            continue;
        }

        const ClipboardItem &item = widget->getItem();
        bool matchKeyword = currentKeyword_.isEmpty() || item.contains(currentKeyword_);
        bool matchType = (currentTypeFilter_ == ClipboardItem::All) || (item.getContentType() == currentTypeFilter_);
        widget->setVisible(matchKeyword && matchType);
        if (widget->isVisible()) {
            ++visibleCount;
        }
    }
    this->setFirstVisibleItemSelected();

    emit itemCountChanged(visibleCount);
}

void ScrollItemsWidget::setShortcutInfo() {
    for (int i = 0, c = 0; i < this->layout->count() - 1 && c < 10; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->isVisible()) {
            widget->setShortcutInfo((c + 1) % 10);
            ++c;
        }
    }
}

void ScrollItemsWidget::cleanShortCutInfo() {
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget) {
            widget->clearShortcutInfo();
        }
    }
}

void ScrollItemsWidget::loadFromSaveDir() {
    this->checkSaveDir();

    while (this->layout->count() > 1) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(0)->widget());
        if (!widget) {
            break;
        }
        this->layout->removeWidget(widget);
        widget->deleteLater();
    }

    pendingLoadFilePaths_.clear();
    totalItemCount_ = 0;
    currItemWidget = nullptr;

    QDir saveDir(this->saveDir());
    const QFileInfoList fileInfos = saveDir.entryInfoList(QStringList() << "*.mpaste", QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &info : fileInfos) {
        pendingLoadFilePaths_ << info.filePath();
    }

    totalItemCount_ = pendingLoadFilePaths_.size();
    trimToMaxSize();
    loadNextBatch(INITIAL_LOAD_BATCH_SIZE);
    maybeLoadMoreItems();
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
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget) {
            widget->setVisible(true);
        }
    }
    if (this->layout->count() > 1) {
        this->setSelectedItem(dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(0)->widget()));
    }
}

const ClipboardItem* ScrollItemsWidget::selectedByShortcut(int keyIndex) {
    if (keyIndex >= 0 && keyIndex < this->layout->count() - 1) {
        for (int i = 0, c = 0; i < this->layout->count() - 1; ++i) {
            auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
            if (widget && widget->isVisible()) {
                if (c == keyIndex) {
                    this->moveItemToFirst(widget);
                    return &widget->getItem();
                }
                ++c;
            }
        }
    }
    if (this->currItemWidget) {
        return &this->currItemWidget->getItem();
    }
    return nullptr;
}

void ScrollItemsWidget::removeItemByContent(const ClipboardItem &item) {
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->getItem() == item) {
            this->saver->removeItem(this->getItemFilePath(widget->getItem()));
            this->layout->removeWidget(widget);
            if (this->currItemWidget == widget) {
                this->currItemWidget = nullptr;
            }
            widget->deleteLater();
            this->layout->update();
            if (this->totalItemCount_ > 0) {
                --this->totalItemCount_;
            }
            if (!this->currItemWidget) {
                this->setFirstVisibleItemSelected();
            }
            this->maybeLoadMoreItems();
            emit itemCountChanged(this->itemCountForDisplay());
            return;
        }
    }

    const QString filePath = this->getItemFilePath(item);
    const int pendingIndex = pendingLoadFilePaths_.indexOf(filePath);
    if (pendingIndex >= 0) {
        pendingLoadFilePaths_.removeAt(pendingIndex);
        this->saver->removeItem(filePath);
        if (this->totalItemCount_ > 0) {
            --this->totalItemCount_;
        }
        emit itemCountChanged(this->itemCountForDisplay());
    }
}

void ScrollItemsWidget::setItemFavorite(const ClipboardItem &item, bool favorite) {
    Q_UNUSED(favorite);
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->getItem() == item) {
            widget->setFavorite(favorite);
            return;
        }
    }
}

QString ScrollItemsWidget::saveDir() {
    return QDir::cleanPath(MPasteSettings::getInst()->getSaveDir() + QDir::separator() + this->category);
}

void ScrollItemsWidget::animateScrollTo(int targetValue) {
    auto *scrollBar = ui->scrollArea->horizontalScrollBar();
    if (!scrollBar) {
        return;
    }

    targetValue = qBound(scrollBar->minimum(), targetValue, scrollBar->maximum());
    const int currentValue = scrollBar->value();
    if (currentValue == targetValue) {
        return;
    }

    scrollAnimation->stop();

    const int distance = qAbs(targetValue - currentValue);
    scrollAnimation->setDuration(qBound(28, 24 + distance / 12, 70));
    scrollAnimation->setStartValue(currentValue);
    scrollAnimation->setEndValue(targetValue);
    scrollAnimation->start();
}

int ScrollItemsWidget::wheelStepPixels() const {
    const int viewportWidth = ui->scrollArea && ui->scrollArea->viewport() ? ui->scrollArea->viewport()->width() : 0;

    int itemWidth = 0;
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget *>(this->layout->itemAt(i)->widget());
        if (widget && widget->isVisible()) {
            itemWidth = widget->width();
            break;
        }
    }

    const int viewportStep = viewportWidth > 0 ? (viewportWidth * 2) / 5 : 480;
    const int itemStep = itemWidth > 0 ? (itemWidth * 3) / 2 : 0;
    const int scrollbarStep = ui->scrollArea->horizontalScrollBar()->singleStep() * 8;

    return qMax(scrollbarStep, qMax(viewportStep, itemStep));
}

void ScrollItemsWidget::loadNextBatch(int batchSize) {
    if (batchSize <= 0 || pendingLoadFilePaths_.isEmpty()) {
        return;
    }

    const int count = qMin(batchSize, pendingLoadFilePaths_.size());
    for (int i = 0; i < count; ++i) {
        const QString filePath = pendingLoadFilePaths_.takeFirst();
        ClipboardItem item = this->saver->loadFromFile(filePath);
        if (item.getName().isEmpty() || !item.getMimeData()) {
            this->saver->removeItem(filePath);
            if (this->totalItemCount_ > 0) {
                --this->totalItemCount_;
            }
            continue;
        }
        auto *itemWidget = createItemWidget(item);
        this->layout->insertWidget(this->layout->count() - 1, itemWidget);
    }

    if (!this->currItemWidget) {
        this->setFirstVisibleItemSelected();
    }

    emit itemCountChanged(this->itemCountForDisplay());
}

void ScrollItemsWidget::ensureAllItemsLoaded() {
    while (!pendingLoadFilePaths_.isEmpty()) {
        loadNextBatch(LOAD_BATCH_SIZE);
    }
}

void ScrollItemsWidget::maybeLoadMoreItems() {
    if (pendingLoadFilePaths_.isEmpty()) {
        return;
    }

    if (!currentKeyword_.isEmpty() || currentTypeFilter_ != ClipboardItem::All) {
        return;
    }

    auto *scrollBar = ui->scrollArea->horizontalScrollBar();
    if (!scrollBar) {
        return;
    }

    while (!pendingLoadFilePaths_.isEmpty()) {
        const int remaining = scrollBar->maximum() - scrollBar->value();
        if (scrollBar->maximum() > 0 && remaining > LOAD_MORE_THRESHOLD_PX) {
            break;
        }

        const int pendingBefore = pendingLoadFilePaths_.size();
        loadNextBatch(LOAD_BATCH_SIZE);
        if (pendingLoadFilePaths_.size() == pendingBefore) {
            break;
        }
        if (scrollBar->maximum() > 0) {
            break;
        }
    }
}

int ScrollItemsWidget::itemCountForDisplay() const {
    if (currentKeyword_.isEmpty() && currentTypeFilter_ == ClipboardItem::All) {
        return totalItemCount_;
    }

    int visibleCount = 0;
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->isVisible()) {
            ++visibleCount;
        }
    }
    return visibleCount;
}

void ScrollItemsWidget::trimToMaxSize() {
    const int maxSize = MPasteSettings::getInst()->getMaxSize();
    while (this->totalItemCount_ > maxSize) {
        if (!pendingLoadFilePaths_.isEmpty()) {
            this->saver->removeItem(pendingLoadFilePaths_.takeLast());
            --this->totalItemCount_;
            continue;
        }

        const int lastIndex = this->layout->count() - 2;
        auto *widget = lastIndex >= 0 ? dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(lastIndex)->widget()) : nullptr;
        if (!widget) {
            break;
        }

        this->saver->removeItem(this->getItemFilePath(widget->getItem()));
        this->layout->removeWidget(widget);
        if (this->currItemWidget == widget) {
            this->currItemWidget = nullptr;
        }
        widget->deleteLater();
        --this->totalItemCount_;
    }

    if (!this->currItemWidget) {
        this->setFirstVisibleItemSelected();
    }
}

bool ScrollItemsWidget::handleWheelScroll(QWheelEvent *event) {
    if (!event) {
        return false;
    }

    auto *scrollBar = ui->scrollArea->horizontalScrollBar();
    if (!scrollBar || (scrollBar->maximum() <= 0 && pendingLoadFilePaths_.isEmpty())) {
        return false;
    }

    int delta = 0;
    const QPoint pixelDelta = event->pixelDelta();
    if (!pixelDelta.isNull()) {
        delta = qRound((qAbs(pixelDelta.x()) > qAbs(pixelDelta.y()) ? -pixelDelta.x() : -pixelDelta.y()) * 1.6);
    } else {
        const QPoint angleDelta = event->angleDelta();
        const int dominantDelta = qAbs(angleDelta.x()) > qAbs(angleDelta.y()) ? angleDelta.x() : angleDelta.y();
        if (dominantDelta != 0) {
            delta = qRound((-dominantDelta / 120.0) * wheelStepPixels());
        }
    }

    if (delta == 0) {
        return false;
    }

    animateScrollTo(scrollBar->value() + delta);
    event->accept();
    return true;
}

void ScrollItemsWidget::focusMoveLeft() {
    if (this->currItemWidget == nullptr) {
        this->setFirstVisibleItemSelected();
    } else {
        int index = this->layout->indexOf(this->currItemWidget);
        if (index > 0 && index <= this->layout->count() - 2) {
            auto *nextWidget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(index - 1)->widget());
            this->setSelectedItem(nextWidget);
            int nextWidgetLeft = nextWidget->pos().x();
            if (nextWidgetLeft < ui->scrollArea->horizontalScrollBar()->value()
                    || nextWidgetLeft > ui->scrollArea->horizontalScrollBar()->value() + ui->scrollArea->width()) {
                animateScrollTo(nextWidgetLeft);
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
            auto *nextWidget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(index + 1)->widget());
            this->setSelectedItem(nextWidget);
            int currValue = ui->scrollArea->horizontalScrollBar()->value() + ui->scrollArea->width();
            int nextWidgetRight = nextWidget->pos().x() + nextWidget->width();
            if (nextWidgetRight > currValue || nextWidgetRight < ui->scrollArea->horizontalScrollBar()->value()) {
                animateScrollTo(nextWidgetRight - ui->scrollArea->width());
            }
        }
    }
}

int ScrollItemsWidget::getItemCount() {
    return this->itemCountForDisplay();
}

void ScrollItemsWidget::scrollToFirst() {
    if (this->layout->count() <= 1) return;

    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->isVisible()) {
            this->setSelectedItem(widget);
            animateScrollTo(0);
            break;
        }
    }
}

void ScrollItemsWidget::scrollToLast() {
    ensureAllItemsLoaded();
    if (this->layout->count() <= 1) return;

    for (int i = this->layout->count() - 2; i >= 0; --i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->isVisible()) {
            this->setSelectedItem(widget);
            int maxScroll = ui->scrollArea->horizontalScrollBar()->maximum();
            animateScrollTo(maxScroll);
            break;
        }
    }
}

QString ScrollItemsWidget::getCategory() const {
    return this->category;
}

void ScrollItemsWidget::selectedByEnter() {
    if (this->currItemWidget != nullptr) {
        this->moveItemToFirst(this->currItemWidget);
    }
}

bool ScrollItemsWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel
        && (watched == ui->scrollArea || watched == ui->scrollArea->viewport())) {
        return handleWheelScroll(static_cast<QWheelEvent *>(event));
    }

    if (event->type() == QEvent::KeyPress) {
        QGuiApplication::sendEvent(this->parent(), event);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
