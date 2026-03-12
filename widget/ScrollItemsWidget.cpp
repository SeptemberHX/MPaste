// input: Depends on ScrollItemsWidget.h, LocalSaver, Qt scrolling APIs, and item-card widgets.
// output: Implements lazy-loaded boards, fingerprint-based dedup, cached filtering, and plain-text paste forwarding.
// pos: Widget-layer board implementation driving clipboard and favorites history lists.
// update: If I change, update this header block and my folder README.md.
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QDebug>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QPainter>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QScrollBar>
#include <QScreen>
#include <QScroller>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QTextDocument>
#include <QThread>
#include <QWheelEvent>
#include <QImage>

#include <utils/MPasteSettings.h>

#include "ScrollItemsWidget.h"
#include "ui_ScrollItemsWidget.h"
#include "ClipboardItemWidget.h"

namespace {
qreal htmlPreviewZoom(qreal devicePixelRatio) {
    return qMax<qreal>(1.0, devicePixelRatio);
}

QDateTime itemTimestampForFile(const QFileInfo &info) {
    bool ok = false;
    const qint64 epochMs = info.completeBaseName().toLongLong(&ok);
    if (ok && epochMs > 0) {
        const QDateTime parsed = QDateTime::fromMSecsSinceEpoch(epochMs);
        if (parsed.isValid()) {
            return parsed;
        }
    }
    return info.lastModified();
}

bool isExpiredForCutoff(const QFileInfo &info, const QDateTime &cutoff) {
    return cutoff.isValid() && itemTimestampForFile(info) < cutoff;
}

QString firstHtmlImageSource(const QString &html) {
    static const QRegularExpression srcRegex(
        QStringLiteral(R"(<img[^>]+src\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = srcRegex.match(html);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

qreal maxScreenDevicePixelRatio() {
    qreal dpr = 1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen) {
            dpr = qMax(dpr, screen->devicePixelRatio());
        }
    }
    return dpr;
}

class EdgeFadeOverlay final : public QWidget {
public:
    enum Side {
        Left,
        Right
    };

    explicit EdgeFadeOverlay(Side side, QWidget *parent = nullptr)
        : QWidget(parent), side_(side) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        const QPoint startPoint = side_ == Left ? rect().topLeft() : rect().topRight();
        const QPoint endPoint = side_ == Left ? rect().topRight() : rect().topLeft();
        QLinearGradient gradient(startPoint, endPoint);

        const QColor mistColor(232, 236, 239);
        QColor solid = mistColor;
        solid.setAlpha(236);
        QColor dense = mistColor;
        dense.setAlpha(192);
        QColor soft = mistColor;
        soft.setAlpha(104);
        QColor faint = mistColor;
        faint.setAlpha(36);
        QColor transparent = mistColor;
        transparent.setAlpha(0);

        gradient.setColorAt(0.00, solid);
        gradient.setColorAt(0.18, solid);
        gradient.setColorAt(0.38, dense);
        gradient.setColorAt(0.62, soft);
        gradient.setColorAt(0.84, faint);
        gradient.setColorAt(1.00, transparent);
        painter.fillRect(rect(), gradient);
    }

private:
    Side side_;
};

QPixmap buildCardThumbnail(const ClipboardItem &item) {
    if (item.hasThumbnail()) {
        return item.thumbnail();
    }

    const QPixmap fullImage = item.getImage();
    if (fullImage.isNull()) {
        return QPixmap();
    }

    constexpr int cardW = 275;
    constexpr int cardH = 218;
    qreal thumbnailDpr = 1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen) {
            thumbnailDpr = qMax(thumbnailDpr, screen->devicePixelRatio());
        }
    }
    const QSize pixelTargetSize = QSize(cardW, cardH) * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return fullImage;
    }

    QPixmap scaled = fullImage.scaled(pixelTargetSize,
        Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return QPixmap();
    }

    const int x = qMax(0, (scaled.width() - pixelTargetSize.width()) / 2);
    const int y = qMax(0, (scaled.height() - pixelTargetSize.height()) / 2);
    QPixmap thumbnail = scaled.copy(x, y,
                                    qMin(scaled.width(), pixelTargetSize.width()),
                                    qMin(scaled.height(), pixelTargetSize.height()));
    thumbnail.setDevicePixelRatio(thumbnailDpr);
    return thumbnail;
}

QPixmap buildRichTextThumbnail(const ClipboardItem &item) {
    const QMimeData *mimeData = item.getMimeData();
    if (!mimeData || !mimeData->hasHtml()) {
        return QPixmap();
    }

    const QString html = mimeData->html();
    if (html.isEmpty()) {
        return QPixmap();
    }

    constexpr int cardW = 275;
    constexpr int cardH = 218;
    qreal thumbnailDpr = 1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen) {
            thumbnailDpr = qMax(thumbnailDpr, screen->devicePixelRatio());
        }
    }

    const QSize pixelTargetSize = QSize(cardW, cardH) * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return QPixmap();
    }

    const qreal previewZoom = htmlPreviewZoom(thumbnailDpr);
    const int leftPadding = qRound(10 * thumbnailDpr);
    const int rightPadding = qRound(10 * thumbnailDpr);
    const int topPadding = qRound(6 * thumbnailDpr);
    const int bottomPadding = qRound(2 * thumbnailDpr);
    const QSize contentSize(
        qMax(1, pixelTargetSize.width() - leftPadding - rightPadding),
        qMax(1, pixelTargetSize.height() - topPadding - bottomPadding));
    const QSizeF layoutSize(
        qMax(1.0, contentSize.width() / previewZoom),
        qMax(1.0, contentSize.height() / previewZoom));

    QTextDocument document;
    document.setDocumentMargin(0);
    document.setDefaultStyleSheet(QStringLiteral("body, p, div, ul, ol, li { margin: 0; padding: 0; }"));
    const QString imageSource = firstHtmlImageSource(html);
    const QByteArray imageBytes = item.imagePayloadBytesFast();
    if (!imageSource.isEmpty() && !imageBytes.isEmpty()) {
        QImage image;
        if (image.loadFromData(imageBytes)) {
            document.addResource(QTextDocument::ImageResource, QUrl(imageSource), image);
        }
    }
    document.setHtml(html);
    document.setPageSize(layoutSize);
    document.setTextWidth(layoutSize.width());

    QPixmap snapshot(pixelTargetSize);
    snapshot.fill(Qt::transparent);

    QPainter painter(&snapshot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.translate(leftPadding, topPadding);
    painter.scale(previewZoom, previewZoom);
    painter.setClipRect(QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    document.drawContents(&painter, QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    painter.end();

    snapshot.setDevicePixelRatio(thumbnailDpr);
    return snapshot;
}

QImage buildCardThumbnailImage(const QImage &sourceImage, qreal targetDpr) {
    if (sourceImage.isNull()) {
        return QImage();
    }

    constexpr int cardW = 275;
    constexpr int cardH = 218;
    const QSize pixelTargetSize = QSize(cardW, cardH) * targetDpr;
    if (!pixelTargetSize.isValid()) {
        return sourceImage;
    }

    QImage scaled = sourceImage.scaled(pixelTargetSize,
        Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return QImage();
    }

    const int x = qMax(0, (scaled.width() - pixelTargetSize.width()) / 2);
    const int y = qMax(0, (scaled.height() - pixelTargetSize.height()) / 2);
    return scaled.copy(x, y,
                       qMin(scaled.width(), pixelTargetSize.width()),
                       qMin(scaled.height(), pixelTargetSize.height()));
}

QImage decodeImagePayload(const ClipboardItem &item) {
    const QByteArray imageBytes = item.imagePayloadBytesFast();
    if (imageBytes.isEmpty()) {
        return QImage();
    }

    QImage image;
    image.loadFromData(imageBytes);
    return image;
}

QImage buildRichTextThumbnailImage(const ClipboardItem &item) {
    const QMimeData *mimeData = item.getMimeData();
    if (!mimeData || !mimeData->hasHtml()) {
        return QImage();
    }

    const QString html = mimeData->html();
    if (html.isEmpty()) {
        return QImage();
    }

    constexpr int cardW = 275;
    constexpr int cardH = 218;
    qreal thumbnailDpr = 1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen) {
            thumbnailDpr = qMax(thumbnailDpr, screen->devicePixelRatio());
        }
    }

    const QSize pixelTargetSize = QSize(cardW, cardH) * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return QImage();
    }

    const qreal previewZoom = htmlPreviewZoom(thumbnailDpr);
    const int leftPadding = qRound(10 * thumbnailDpr);
    const int rightPadding = qRound(10 * thumbnailDpr);
    const int topPadding = qRound(6 * thumbnailDpr);
    const int bottomPadding = qRound(2 * thumbnailDpr);
    const QSize contentSize(
        qMax(1, pixelTargetSize.width() - leftPadding - rightPadding),
        qMax(1, pixelTargetSize.height() - topPadding - bottomPadding));
    const QSizeF layoutSize(
        qMax(1.0, contentSize.width() / previewZoom),
        qMax(1.0, contentSize.height() / previewZoom));

    QTextDocument document;
    document.setDocumentMargin(0);
    document.setDefaultStyleSheet(QStringLiteral("body, p, div, ul, ol, li { margin: 0; padding: 0; }"));
    document.setHtml(html);
    document.setPageSize(layoutSize);
    document.setTextWidth(layoutSize.width());

    QImage snapshot(pixelTargetSize, QImage::Format_ARGB32_Premultiplied);
    snapshot.fill(Qt::transparent);

    QPainter painter(&snapshot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.translate(leftPadding, topPadding);
    painter.scale(previewZoom, previewZoom);
    painter.setClipRect(QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    document.drawContents(&painter, QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    painter.end();

    return snapshot;
}

struct PendingItemProcessingResult {
    QImage thumbnailImage;
};

ClipboardItem prepareItemForDisplayAndSave(const ClipboardItem &source) {
    ClipboardItem item(source);
    if (!item.hasThumbnail() && item.getContentType() == ClipboardItem::Image) {
        const QPixmap thumbnail = buildCardThumbnail(item);
        if (!thumbnail.isNull()) {
            item.setThumbnail(thumbnail);
        }
    } else if (!item.hasThumbnail() && item.getContentType() == ClipboardItem::RichText) {
        const QPixmap thumbnail = buildRichTextThumbnail(item);
        if (!thumbnail.isNull()) {
            item.setThumbnail(thumbnail);
        }
    }
    return item;
}
}

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
    const QSize itemOuterSize = ClipboardItemWidget::scaledOuterSize(scale);
    const int scrollbarHeight = qMax(12, ui->scrollArea->horizontalScrollBar()->sizeHint().height());
    const int scrollHeight = itemOuterSize.height() + scrollbarHeight;
    edgeContentPadding_ = qMax(6, 8 * scale / 100);
    edgeFadeWidth_ = qMax(12, 16 * scale / 100);
    ui->scrollArea->setFixedHeight(scrollHeight);
    ui->scrollAreaWidgetContents->setMinimumHeight(itemOuterSize.height());
    setFixedHeight(scrollHeight);

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

    deferredLoadTimer_ = new QTimer(this);
    deferredLoadTimer_->setSingleShot(true);
    connect(deferredLoadTimer_, &QTimer::timeout, this, &ScrollItemsWidget::continueDeferredLoad);

    this->layout = new QHBoxLayout(ui->scrollAreaWidgetContents);
    this->layout->setContentsMargins(edgeContentPadding_, 0, edgeContentPadding_, 0);
    this->layout->setSpacing(qMax(6, 8 * scale / 100));
    this->layout->addStretch(1);
    this->saver = new LocalSaver();
    updateContentWidthHint();

    leftEdgeFadeOverlay_ = new EdgeFadeOverlay(EdgeFadeOverlay::Left, ui->scrollArea->viewport());
    rightEdgeFadeOverlay_ = new EdgeFadeOverlay(EdgeFadeOverlay::Right, ui->scrollArea->viewport());
    updateEdgeFadeOverlays();
    leftEdgeFadeOverlay_->show();
    rightEdgeFadeOverlay_->show();

    ui->scrollArea->installEventFilter(this);
    ui->scrollArea->viewport()->installEventFilter(this);
    connect(ui->scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) { maybeLoadMoreItems(); });
}

ScrollItemsWidget::~ScrollItemsWidget()
{
    deferredLoadActive_ = false;
    if (deferredLoadThread_) {
        deferredLoadThread_->wait();
    }
    for (QThread *thread : processingThreads_) {
        if (thread) {
            thread->wait();
        }
    }
    delete ui;
}

ClipboardItemWidget *ScrollItemsWidget::findMatchingWidget(const ClipboardItem &item) const {
    const auto it = fingerprintBuckets_.constFind(item.fingerprint());
    if (it == fingerprintBuckets_.constEnd()) {
        return nullptr;
    }

    for (ClipboardItemWidget *widget : it.value()) {
        if (widget && widget->getItem() == item) {
            return widget;
        }
    }

    return nullptr;
}

ClipboardItemWidget *ScrollItemsWidget::findWidgetByName(const QString &name) const {
    if (name.isEmpty()) {
        return nullptr;
    }

    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->getItem().getName() == name) {
            return widget;
        }
    }
    return nullptr;
}

void ScrollItemsWidget::registerWidgetFingerprint(ClipboardItemWidget *widget) {
    if (!widget) {
        return;
    }

    auto &bucket = fingerprintBuckets_[widget->getItem().fingerprint()];
    if (!bucket.contains(widget)) {
        bucket.append(widget);
    }
}

void ScrollItemsWidget::updateWidgetItem(ClipboardItemWidget *widget, const ClipboardItem &item) {
    if (!widget) {
        return;
    }

    unregisterWidgetFingerprint(widget);
    widget->showItem(item);
    registerWidgetFingerprint(widget);
}

void ScrollItemsWidget::unregisterWidgetFingerprint(ClipboardItemWidget *widget) {
    if (!widget) {
        return;
    }

    const QByteArray key = widget->getItem().fingerprint();
    auto it = fingerprintBuckets_.find(key);
    if (it == fingerprintBuckets_.end()) {
        return;
    }

    it->removeAll(widget);
    if (it->isEmpty()) {
        fingerprintBuckets_.erase(it);
    }
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
        this->unregisterWidgetFingerprint(itemWidget);
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
        this->refreshContentWidthHint();

        emit itemCountChanged(this->itemCountForDisplay());
    });
    connect(itemWidget, &ClipboardItemWidget::itemStared, this, &ScrollItemsWidget::itemStared);
    connect(itemWidget, &ClipboardItemWidget::itemUnstared, this, &ScrollItemsWidget::itemUnstared);
    connect(itemWidget, &ClipboardItemWidget::pastePlainTextRequested, this, [this, itemWidget](const ClipboardItem &) {
        this->moveItemToFirst(itemWidget);
        emit plainTextPasteRequested(itemWidget->getItem());
    });
    connect(itemWidget, &ClipboardItemWidget::detailsRequested, this, [this, itemWidget](const ClipboardItem &) {
        this->setSelectedItem(itemWidget);
        const QPair<int, int> sequenceInfo = displaySequenceForWidget(itemWidget);
        emit detailsRequested(itemWidget->getItem(), sequenceInfo.first, sequenceInfo.second);
    });

    itemWidget->installEventFilter(this);
    itemWidget->showItem(item);

    if (this->category == MPasteSettings::STAR_CATEGORY_NAME
        || favoriteFingerprints_.contains(item.fingerprint())) {
        itemWidget->setFavorite(true);
    }

    return itemWidget;
}

bool ScrollItemsWidget::addOneItem(const ClipboardItem &nItem) {
    if (auto *widget = findMatchingWidget(nItem)) {
        if (nItem.getMimeData() && widget->getItem().getMimeData()
            && nItem.getMimeData()->hasHtml() && widget->getItem().getMimeData()->hasHtml()
            && nItem.getMimeData()->html().length() > widget->getItem().getMimeData()->html().length()) {
            this->saver->removeItem(this->getItemFilePath(widget->getItem()));
            this->updateWidgetItem(widget, nItem);
            if (!nItem.isMimeDataLoaded() && nItem.sourceFilePath().isEmpty()) {
                processPendingItemAsync(nItem, widget);
            } else {
                this->saveItem(nItem);
            }
        }
        const int index = this->layout->indexOf(widget);
        if (index > 0) {
            this->moveItemToFirst(widget);
        }
        return false;
    }

    auto *itemWidget = createItemWidget(nItem);
    this->layout->insertWidget(0, itemWidget);
    this->registerWidgetFingerprint(itemWidget);
    this->setSelectedItem(itemWidget);

    ++this->totalItemCount_;
    this->trimExpiredItems();
    if (!this->currItemWidget) {
        this->setFirstVisibleItemSelected();
    }
    this->refreshContentWidthHint();

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

    // Force full MIME load while the source file still exists on disk
    widget->getItem().getMimeData();

    ClipboardItem item = prepareItemForDisplayAndSave(widget->getItem());
    this->saver->removeItem(this->getItemFilePath(widget->getItem()));
    this->saveItem(item);
    this->layout->removeWidget(widget);
    this->layout->insertWidget(0, widget);
    this->setSelectedItem(widget);
    this->updateWidgetItem(widget, item);
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
    this->refreshContentWidthHint();

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

void ScrollItemsWidget::processPendingItemAsync(const ClipboardItem &item, ClipboardItemWidget *targetWidget) {
    const QString filePath = this->getItemFilePath(item);
    const QString expectedName = item.getName();
    QPointer<ScrollItemsWidget> guard(this);
    QPointer<ClipboardItemWidget> widgetGuard(targetWidget);
    qInfo().noquote() << QStringLiteral("[scroll-items] async process queued category=%1 name=%2 type=%3 hasThumb=%4 path=%5")
        .arg(category)
        .arg(expectedName)
        .arg(item.getContentType())
        .arg(item.hasThumbnail())
        .arg(filePath);

    QThread *thread = QThread::create([guard, widgetGuard, item, filePath, expectedName]() mutable {
        QElapsedTimer timer;
        timer.start();
        PendingItemProcessingResult result;
        if (item.getContentType() == ClipboardItem::Image) {
            result.thumbnailImage = buildCardThumbnailImage(decodeImagePayload(item), maxScreenDevicePixelRatio());
        } else if (item.getContentType() == ClipboardItem::RichText) {
            result.thumbnailImage = buildRichTextThumbnailImage(item);
        }
        qInfo().noquote() << QStringLiteral("[scroll-items] async process finished name=%1 type=%2 hasThumb=%3 elapsedMs=%4 path=%5")
            .arg(expectedName)
            .arg(item.getContentType())
            .arg(!result.thumbnailImage.isNull())
            .arg(timer.elapsed())
            .arg(filePath);

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, widgetGuard, filePath, expectedName, item, result]() mutable {
                if (!guard) {
                    return;
                }

                ClipboardItem preparedItem = item;
                if (!result.thumbnailImage.isNull()) {
                    QPixmap thumbnail = QPixmap::fromImage(result.thumbnailImage);
                    thumbnail.setDevicePixelRatio(qMax<qreal>(1.0, qApp->devicePixelRatio()));
                    preparedItem.setThumbnail(thumbnail);
                }
                guard->saveItem(preparedItem);

                ClipboardItem reloadedItem = guard->saver->loadFromFileLight(filePath);
                if (reloadedItem.getName().isEmpty() || reloadedItem.getName() != expectedName) {
                    return;
                }

                ClipboardItemWidget *widget = nullptr;
                if (widgetGuard && widgetGuard->getItem().getName() == expectedName) {
                    widget = widgetGuard;
                } else {
                    widget = guard->findWidgetByName(expectedName);
                }
                if (widget) {
                    guard->updateWidgetItem(widget, reloadedItem);
                }
            }, Qt::QueuedConnection);
        }
    });

    processingThreads_.append(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        processingThreads_.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ScrollItemsWidget::loadFromSaveDir() {
    deferredLoadActive_ = false;
    deferredLoadTimer_->stop();
    QElapsedTimer timer;
    timer.start();
    qInfo().noquote() << QStringLiteral("[scroll-items] loadFromSaveDir begin category=%1").arg(category);
    prepareLoadFromSaveDir();
    loadNextBatch(INITIAL_LOAD_BATCH_SIZE);
    maybeLoadMoreItems();
    qInfo().noquote() << QStringLiteral("[scroll-items] loadFromSaveDir end category=%1 totalCount=%2 visibleCount=%3 elapsedMs=%4")
        .arg(category)
        .arg(totalItemCount_)
        .arg(itemCountForDisplay())
        .arg(timer.elapsed());
}

void ScrollItemsWidget::loadFromSaveDirDeferred() {
    deferredLoadActive_ = true;
    deferredLoadTimer_->stop();
    QElapsedTimer timer;
    timer.start();
    qInfo().noquote() << QStringLiteral("[scroll-items] loadFromSaveDirDeferred begin category=%1").arg(category);
    prepareLoadFromSaveDir();

    if (pendingLoadFilePaths_.isEmpty()) {
        deferredLoadActive_ = false;
        qInfo().noquote() << QStringLiteral("[scroll-items] loadFromSaveDirDeferred end category=%1 totalCount=%2 visibleCount=%3 elapsedMs=%4")
            .arg(category)
            .arg(totalItemCount_)
            .arg(itemCountForDisplay())
            .arg(timer.elapsed());
        return;
    }

    loadNextBatch(qMin(DEFERRED_LOAD_BATCH_SIZE, INITIAL_LOAD_BATCH_SIZE));
    if (shouldKeepDeferredLoading()) {
        scheduleDeferredLoadBatch();
    } else {
        deferredLoadActive_ = false;
    }
    qInfo().noquote() << QStringLiteral("[scroll-items] loadFromSaveDirDeferred end category=%1 totalCount=%2 visibleCount=%3 elapsedMs=%4 deferredActive=%5")
        .arg(category)
        .arg(totalItemCount_)
        .arg(itemCountForDisplay())
        .arg(timer.elapsed())
        .arg(deferredLoadActive_);
}

bool ScrollItemsWidget::appendLoadedItem(const QString &filePath, const ClipboardItem &item) {
    if (findMatchingWidget(item)) {
        this->saver->removeItem(filePath);
        if (this->totalItemCount_ > 0) {
            --this->totalItemCount_;
        }
        return false;
    }

    auto *itemWidget = createItemWidget(item);
    this->layout->insertWidget(this->layout->count() - 1, itemWidget);
    this->registerWidgetFingerprint(itemWidget);
    return true;
}

QPair<int, int> ScrollItemsWidget::displaySequenceForWidget(const ClipboardItemWidget *widget) const {
    if (!widget) {
        return qMakePair(-1, itemCountForDisplay());
    }

    int visibleIndex = 0;
    int visibleTotal = 0;
    int absoluteIndex = 0;
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *candidate = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (!candidate) {
            continue;
        }

        ++absoluteIndex;
        if (candidate->isVisible()) {
            ++visibleTotal;
            if (candidate == widget) {
                visibleIndex = visibleTotal;
            }
        } else if (candidate == widget) {
            visibleIndex = absoluteIndex;
        }
    }

    if (visibleIndex <= 0) {
        return qMakePair(-1, visibleTotal > 0 ? visibleTotal : absoluteIndex);
    }

    return qMakePair(visibleIndex, visibleTotal > 0 ? visibleTotal : absoluteIndex);
}

QScrollBar* ScrollItemsWidget::horizontalScrollbar() {
    return ui->scrollArea->horizontalScrollBar();
}

bool ScrollItemsWidget::addAndSaveItem(const ClipboardItem &nItem) {
    const bool isPendingClipboardSnapshot = !nItem.isMimeDataLoaded() && nItem.sourceFilePath().isEmpty();
    ClipboardItem preparedItem = isPendingClipboardSnapshot ? nItem : prepareItemForDisplayAndSave(nItem);
    const bool added = this->addOneItem(preparedItem);
    if (added) {
        if (isPendingClipboardSnapshot) {
            processPendingItemAsync(preparedItem, findMatchingWidget(preparedItem));
        } else {
            this->saveItem(preparedItem);
        }
    }
    return added;
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

const ClipboardItem* ScrollItemsWidget::currentSelectedItem() const {
    if (this->currItemWidget) {
        return &this->currItemWidget->getItem();
    }
    return nullptr;
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
            this->unregisterWidgetFingerprint(widget);
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
            this->refreshContentWidthHint();
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
        this->refreshContentWidthHint();
        emit itemCountChanged(this->itemCountForDisplay());
    }
}

void ScrollItemsWidget::setItemFavorite(const ClipboardItem &item, bool favorite) {
    if (favorite) {
        favoriteFingerprints_.insert(item.fingerprint());
    } else {
        favoriteFingerprints_.remove(item.fingerprint());
    }

    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget && widget->getItem().fingerprint() == item.fingerprint()) {
            widget->setFavorite(favorite);
        }
    }
}

QList<ClipboardItem> ScrollItemsWidget::allItems() {
    ensureAllItemsLoaded();

    QList<ClipboardItem> items;
    items.reserve(qMax(0, this->layout->count() - 1));
    for (int i = 0; i < this->layout->count() - 1; ++i) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(i)->widget());
        if (widget) {
            items.append(widget->getItem());
        }
    }

    return items;
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

    QElapsedTimer timer;
    timer.start();
    const int count = qMin(batchSize, pendingLoadFilePaths_.size());
    qInfo().noquote() << QStringLiteral("[scroll-items] loadNextBatch begin category=%1 batchRequest=%2 batchActual=%3 pendingBefore=%4")
        .arg(category)
        .arg(batchSize)
        .arg(count)
        .arg(pendingLoadFilePaths_.size());
    int loadedCount = 0;
    for (int i = 0; i < count; ++i) {
        const QString filePath = pendingLoadFilePaths_.takeFirst();

        // Try light load (V4 files load only header + thumbnail)
        ClipboardItem item = this->saver->loadFromFileLight(filePath);
        if (item.getName().isEmpty()) {
            this->saver->removeItem(filePath);
            if (this->totalItemCount_ > 0) {
                --this->totalItemCount_;
            }
            continue;
        }

        if (appendLoadedItem(filePath, item)) {
            ++loadedCount;
        }
    }

    if (!this->currItemWidget) {
        this->setFirstVisibleItemSelected();
    }

    emit itemCountChanged(this->itemCountForDisplay());
    updateContentWidthHint();
    qInfo().noquote() << QStringLiteral("[scroll-items] loadNextBatch end category=%1 loaded=%2 pendingAfter=%3 elapsedMs=%4")
        .arg(category)
        .arg(loadedCount)
        .arg(pendingLoadFilePaths_.size())
        .arg(timer.elapsed());
}

void ScrollItemsWidget::ensureAllItemsLoaded() {
    deferredLoadActive_ = false;
    deferredLoadTimer_->stop();
    waitForDeferredRead();
    processDeferredLoadedItems();
    while (!pendingLoadFilePaths_.isEmpty()) {
        loadNextBatch(LOAD_BATCH_SIZE);
    }
}

void ScrollItemsWidget::maybeLoadMoreItems() {
    if (deferredLoadActive_) {
        return;
    }

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

void ScrollItemsWidget::prepareLoadFromSaveDir() {
    this->checkSaveDir();

    while (this->layout->count() > 1) {
        auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(0)->widget());
        if (!widget) {
            break;
        }
        this->unregisterWidgetFingerprint(widget);
        this->layout->removeWidget(widget);
        widget->deleteLater();
    }

    pendingLoadFilePaths_.clear();
    fingerprintBuckets_.clear();
    totalItemCount_ = 0;
    currItemWidget = nullptr;

    QDir saveDir(this->saveDir());
    const QFileInfoList fileInfos = saveDir.entryInfoList(QStringList() << "*.mpaste", QDir::Files, QDir::Name | QDir::Reversed);
    int acceptedCount = 0;
    int skippedCount = 0;
    for (const QFileInfo &info : fileInfos) {
        if (LocalSaver::isCurrentFormatFile(info.filePath())) {
            pendingLoadFilePaths_ << info.filePath();
            ++acceptedCount;
        } else {
            ++skippedCount;
        }
    }

    totalItemCount_ = pendingLoadFilePaths_.size();
    trimExpiredItems();
    updateContentWidthHint();
    qInfo().noquote() << QStringLiteral("[scroll-items] prepareLoadFromSaveDir category=%1 files=%2 accepted=%3 skipped=%4")
        .arg(category)
        .arg(fileInfos.size())
        .arg(acceptedCount)
        .arg(skippedCount);
}

void ScrollItemsWidget::continueDeferredLoad() {
    if (!deferredLoadActive_) {
        return;
    }

    qInfo().noquote() << QStringLiteral("[scroll-items] continueDeferredLoad category=%1 queued=%2 pendingPaths=%3")
        .arg(category)
        .arg(deferredLoadedItems_.size())
        .arg(pendingLoadFilePaths_.size());
    processDeferredLoadedItems();

    if (deferredLoadedItems_.isEmpty() && !deferredLoadThread_ && !pendingLoadFilePaths_.isEmpty()) {
        scheduleDeferredLoadBatch();
    }

    if (deferredLoadedItems_.isEmpty() && !deferredLoadThread_ && pendingLoadFilePaths_.isEmpty()) {
        deferredLoadActive_ = false;
    }
}

bool ScrollItemsWidget::shouldKeepDeferredLoading() const {
    if (pendingLoadFilePaths_.isEmpty()) {
        return false;
    }

    if (!currentKeyword_.isEmpty() || currentTypeFilter_ != ClipboardItem::All) {
        return false;
    }

    auto *scrollBar = ui->scrollArea->horizontalScrollBar();
    if (!scrollBar) {
        return false;
    }

    const int remaining = scrollBar->maximum() - scrollBar->value();
    return scrollBar->maximum() <= 0 || remaining <= LOAD_MORE_THRESHOLD_PX;
}

void ScrollItemsWidget::scheduleDeferredLoadBatch() {
    if (!deferredLoadActive_ || deferredLoadThread_ || pendingLoadFilePaths_.isEmpty()) {
        return;
    }

    const int count = qMin(DEFERRED_LOAD_BATCH_SIZE, pendingLoadFilePaths_.size());
    QStringList batchPaths;
    batchPaths.reserve(count);
    for (int i = 0; i < count; ++i) {
        batchPaths << pendingLoadFilePaths_.takeFirst();
    }
    qInfo().noquote() << QStringLiteral("[scroll-items] scheduleDeferredLoadBatch category=%1 batch=%2 pendingAfterTake=%3")
        .arg(category)
        .arg(batchPaths.size())
        .arg(pendingLoadFilePaths_.size());

    QPointer<ScrollItemsWidget> guard(this);
    deferredLoadThread_ = QThread::create([guard, batchPaths]() {
        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, batchPaths]() {
                if (guard) {
                    guard->handleDeferredBatchRead(batchPaths);
                }
            }, Qt::QueuedConnection);
        }
    });

    connect(deferredLoadThread_, &QThread::finished, this, [this]() {
        deferredLoadThread_ = nullptr;
    });
    connect(deferredLoadThread_, &QThread::finished, deferredLoadThread_, &QObject::deleteLater);
    deferredLoadThread_->start();
}

void ScrollItemsWidget::handleDeferredBatchRead(const QStringList &batchPaths) {
    deferredLoadedItems_.append(batchPaths);

    if (!deferredLoadTimer_->isActive()) {
        const bool widgetVisible = isVisible() && window() && window()->isVisible();
        deferredLoadTimer_->start(widgetVisible ? 0 : 8);
    }

    if (deferredLoadActive_ && !pendingLoadFilePaths_.isEmpty()) {
        scheduleDeferredLoadBatch();
    }
}

void ScrollItemsWidget::processDeferredLoadedItems() {
    if (deferredLoadedItems_.isEmpty()) {
        return;
    }

    QElapsedTimer parseTimer;
    parseTimer.start();
    const bool widgetVisible = isVisible() && window() && window()->isVisible();
    const int maxItemsPerTick = widgetVisible ? 2 : 1;
    const int maxParseMs = widgetVisible ? 12 : 4;
    int processedCount = 0;
    int loadedCount = 0;
    const int initialQueuedCount = deferredLoadedItems_.size();
    qInfo().noquote() << QStringLiteral("[scroll-items] processDeferredLoadedItems begin category=%1 queued=%2 visible=%3 maxItems=%4 maxMs=%5")
        .arg(category)
        .arg(initialQueuedCount)
        .arg(widgetVisible)
        .arg(maxItemsPerTick)
        .arg(maxParseMs);
    while (!deferredLoadedItems_.isEmpty() && processedCount < maxItemsPerTick && parseTimer.elapsed() < maxParseMs) {
        const QString filePath = deferredLoadedItems_.takeFirst();
        ClipboardItem item = this->saver->loadFromFileLight(filePath);
        if (item.getName().isEmpty()) {
            this->saver->removeItem(filePath);
            if (this->totalItemCount_ > 0) {
                --this->totalItemCount_;
            }
        } else if (appendLoadedItem(filePath, item)) {
            ++loadedCount;
        }
        ++processedCount;
    }

    if (!deferredLoadedItems_.isEmpty() && (widgetVisible || processedCount > 0)) {
        deferredLoadTimer_->start(widgetVisible ? 0 : 8);
    }

    if (!this->currItemWidget) {
        this->setFirstVisibleItemSelected();
    }

    emit itemCountChanged(this->itemCountForDisplay());
    updateContentWidthHint();
    qInfo().noquote() << QStringLiteral("[scroll-items] processDeferredLoadedItems end category=%1 processed=%2 loaded=%3 remaining=%4 skippedLarge=%5 elapsedMs=%6")
        .arg(category)
        .arg(processedCount)
        .arg(loadedCount)
        .arg(deferredLoadedItems_.size())
        .arg(0)
        .arg(parseTimer.elapsed());
}

void ScrollItemsWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);

    qInfo().noquote() << QStringLiteral("[scroll-items] showEvent category=%1 queuedDeferred=%2 pendingPaths=%3")
        .arg(category)
        .arg(deferredLoadedItems_.size())
        .arg(pendingLoadFilePaths_.size());
    updateEdgeFadeOverlays();
    if (!deferredLoadedItems_.isEmpty() && !deferredLoadTimer_->isActive()) {
        deferredLoadTimer_->start(0);
    }
}

void ScrollItemsWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    qInfo().noquote() << QStringLiteral("[scroll-items] resizeEvent category=%1 size=%2x%3")
        .arg(category)
        .arg(width())
        .arg(height());
    refreshContentWidthHint();
    updateEdgeFadeOverlays();
}

void ScrollItemsWidget::waitForDeferredRead() {
    if (deferredLoadThread_) {
        deferredLoadThread_->wait();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

void ScrollItemsWidget::refreshContentWidthHint() {
    updateContentWidthHint();
    updateEdgeFadeOverlays();

    auto *scrollBar = ui->scrollArea ? ui->scrollArea->horizontalScrollBar() : nullptr;
    if (scrollBar) {
        scrollBar->setValue(qMin(scrollBar->value(), scrollBar->maximum()));
    }
}


void ScrollItemsWidget::updateContentWidthHint() {
    int itemWidth = 0;
    if (this->layout->count() > 1) {
        if (auto *widget = dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(0)->widget())) {
            itemWidth = widget->width();
        }
    }

    if (itemWidth <= 0) {
        const int scale = MPasteSettings::getInst()->getItemScale();
        itemWidth = ClipboardItemWidget::scaledOuterSize(scale).width();
    }

    const int itemCount = (currentKeyword_.isEmpty() && currentTypeFilter_ == ClipboardItem::All)
        ? qMax(totalItemCount_, this->layout->count() - 1)
        : itemCountForDisplay();
    const int spacing = this->layout->spacing();
    const QMargins contentMargins = this->layout->contentsMargins();
    const int viewportWidth = ui->scrollArea && ui->scrollArea->viewport() ? ui->scrollArea->viewport()->width() : 0;
    const int estimatedWidth = itemCount > 0
        ? itemCount * itemWidth + qMax(0, itemCount - 1) * spacing + contentMargins.left() + contentMargins.right()
        : viewportWidth;

    ui->scrollAreaWidgetContents->setMinimumWidth(qMax(viewportWidth, estimatedWidth));
}

void ScrollItemsWidget::updateEdgeFadeOverlays() {
    QWidget *viewport = ui->scrollArea ? ui->scrollArea->viewport() : nullptr;
    if (!viewport || !leftEdgeFadeOverlay_ || !rightEdgeFadeOverlay_) {
        return;
    }

    const int fadeWidth = qMin(edgeFadeWidth_, qMax(0, viewport->width() / 3));
    if (fadeWidth <= 0 || viewport->height() <= 0) {
        leftEdgeFadeOverlay_->hide();
        rightEdgeFadeOverlay_->hide();
        return;
    }

    leftEdgeFadeOverlay_->setGeometry(0, 0, fadeWidth, viewport->height());
    rightEdgeFadeOverlay_->setGeometry(viewport->width() - fadeWidth, 0, fadeWidth, viewport->height());
    leftEdgeFadeOverlay_->raise();
    rightEdgeFadeOverlay_->raise();
    leftEdgeFadeOverlay_->show();
    rightEdgeFadeOverlay_->show();
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

void ScrollItemsWidget::trimExpiredItems() {
    const QDateTime cutoff = MPasteSettings::getInst()->historyRetentionCutoff();
    while (!pendingLoadFilePaths_.isEmpty()) {
        const QFileInfo info(pendingLoadFilePaths_.last());
        if (!isExpiredForCutoff(info, cutoff)) {
            break;
        }

        this->saver->removeItem(pendingLoadFilePaths_.takeLast());
        if (this->totalItemCount_ > 0) {
            --this->totalItemCount_;
        }
    }

    while (true) {
        const int lastIndex = this->layout->count() - 2;
        auto *widget = lastIndex >= 0 ? dynamic_cast<ClipboardItemWidget*>(this->layout->itemAt(lastIndex)->widget()) : nullptr;
        if (!widget) {
            break;
        }

        if (widget->getItem().getTime() >= cutoff) {
            break;
        }

        this->saver->removeItem(this->getItemFilePath(widget->getItem()));
        this->unregisterWidgetFingerprint(widget);
        this->layout->removeWidget(widget);
        if (this->currItemWidget == widget) {
            this->currItemWidget = nullptr;
        }
        widget->deleteLater();
        if (this->totalItemCount_ > 0) {
            --this->totalItemCount_;
        }
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

const ClipboardItem* ScrollItemsWidget::selectedByEnter() {
    if (this->currItemWidget != nullptr) {
        this->moveItemToFirst(this->currItemWidget);
        return &this->currItemWidget->getItem();
    }
    return nullptr;
}

bool ScrollItemsWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel
        && (watched == ui->scrollArea || watched == ui->scrollArea->viewport())) {
        return handleWheelScroll(static_cast<QWheelEvent *>(event));
    }

    if (event->type() == QEvent::Resize
        && (watched == ui->scrollArea || watched == ui->scrollArea->viewport())) {
        refreshContentWidthHint();
    }

    if (event->type() == QEvent::KeyPress) {
        QGuiApplication::sendEvent(this->parent(), event);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
