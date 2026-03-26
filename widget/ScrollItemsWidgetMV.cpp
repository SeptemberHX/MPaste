// input: Depends on ScrollItemsWidget.h, ClipboardBoardService, Qt model/view APIs, and delegate-based card painting.
// output: Implements lazy-loaded boards, fixed-page browsing, proxy filtering, async thumbnail completion, and list-view item interaction.
// pos: Widget-layer board implementation driving clipboard and favorites history lists.
// update: If I change, update this header block and my folder README.md (arrow navigation no longer forces center + hover action bar + main-card save/export + multi-select batch actions + data-layer preview kind + board paint FPS logging + hidden-stage light prewarm).
// note: Added dark theme rendering hooks, metadata-save rehydrate fixes, alias sync, loading overlay, board paint FPS logging, hidden-stage light prewarm, and switchable paged-vs-continuous history loading.
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QListView>
#include <QLabel>
#include <QElapsedTimer>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScroller>
#include <QShowEvent>
#include <QStandardPaths>
#include <QToolButton>
#include <QStyle>
#include <QStyleFactory>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QDateTime>

#include <utils/MPasteSettings.h>

#include "ClipboardBoardModel.h"
#include "ClipboardBoardProxyModel.h"
#include "ClipboardBoardActionService.h"
#include "ClipboardCardDelegate.h"
#include "ClipboardItemPreviewDialog.h"
#include "ClipboardItemRenameDialog.h"
#include "CardMetrics.h"
#include "ScrollItemsWidget.h"
#include "ui_ScrollItemsWidget.h"
#include "utils/OpenGraphFetcher.h"
#include "utils/PlatformRelated.h"
#include "utils/ThemeManager.h"
#include "utils/IconResolver.h"

namespace {

QString hoverButtonStyle(bool dark, int borderRadius, int padding) {
    const QString hoverColor = dark ? QStringLiteral("rgba(255, 255, 255, 31)")
                                    : QStringLiteral("rgba(0, 0, 0, 20)");
    return QString(R"(
        QToolButton {
            background: transparent;
            border: none;
            border-radius: %1px;
            padding: %2px;
        }
        QToolButton:hover {
            background: %3;
        }
    )").arg(borderRadius).arg(padding).arg(hoverColor);
}

void applyMenuTheme(QMenu *menu) {
    if (!menu) {
        return;
    }
    const bool dark = ThemeManager::instance()->isDark();
    QPalette pal = menu->palette();
    if (dark) {
        pal.setColor(QPalette::Window, QColor("#1E232B"));
        pal.setColor(QPalette::WindowText, QColor("#D6DEE8"));
        pal.setColor(QPalette::Base, QColor("#1E232B"));
        pal.setColor(QPalette::AlternateBase, QColor("#1A1F27"));
        pal.setColor(QPalette::Text, QColor("#D6DEE8"));
        pal.setColor(QPalette::Button, QColor("#1E232B"));
        pal.setColor(QPalette::ButtonText, QColor("#D6DEE8"));
        pal.setColor(QPalette::Highlight, QColor("#2D7FD3"));
        pal.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    } else {
        pal = qApp->palette();
    }
    menu->setPalette(pal);
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        fusion->setParent(menu);
        menu->setStyle(fusion);
    }
}

class HoverActionBar final : public QWidget {
public:
    explicit HoverActionBar(QWidget *parent = nullptr)
        : QWidget(parent) {}

    void setCornerRadius(int radius) {
        cornerRadius_ = radius;
        update();
    }

    void setColors(const QColor &background, const QColor &border) {
        background_ = background;
        border_ = border;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF rectF = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(border_);
        painter.setBrush(background_);
        painter.drawRoundedRect(rectF, cornerRadius_, cornerRadius_);
    }

private:
    int cornerRadius_ = 8;
    QColor background_ = QColor(255, 255, 255, 185);
    QColor border_ = QColor(255, 255, 255, 120);
};

bool looksBrokenTranslation(const QString &text) {
    if (text.isEmpty()) {
        return true;
    }

    int suspiciousCount = 0;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('?') || ch == QChar::ReplacementCharacter) {
            ++suspiciousCount;
        }
    }
    return suspiciousCount >= qMax(2, text.size() / 2);
}

bool prefersChineseUi() {
    const QLocale locale = QLocale::system();
    return locale.language() == QLocale::Chinese
        || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);
}

QString plainTextPasteLabel() {
    QString label = QObject::tr("Paste as Plain Text");
    const QLocale locale = QLocale::system();
    if (label == QLatin1String("Paste as Plain Text") || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return QString::fromUtf16(u"\u7EAF\u6587\u672C\u7C98\u8D34");
        }
        return QStringLiteral("Paste as Plain Text");
    }
    return label;
}

QString detailsLabel() {
    QString label = QObject::tr("Details");
    const QLocale locale = QLocale::system();
    if (label == QLatin1String("Details") || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return QString::fromUtf16(u"\u8BE6\u60C5");
        }
        return QStringLiteral("Details");
    }
    return label;
}

QString openContainingFolderLabel() {
    QString label = QObject::tr("Open Containing Folder");
    const QLocale locale = QLocale::system();
    if (label == QLatin1String("Open Containing Folder") || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return QString::fromUtf16(u"\u6253\u5F00\u6240\u5728\u6587\u4EF6\u5939");
        }
        return QStringLiteral("Open Containing Folder");
    }
    return label;
}

QString aliasLabel() {
    QString label = QObject::tr("Alias");
    const QLocale locale = QLocale::system();
    if (label == QLatin1String("Alias") || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return QString::fromUtf16(u"\u522B\u540D");
        }
        return QStringLiteral("Alias");
    }
    return label;
}

QString favoriteActionLabel(bool favorite) {
    const QString key = favorite ? QStringLiteral("Remove from favorites") : QStringLiteral("Add to favorites");
    QString label = QObject::tr(key.toUtf8().constData());
    const QLocale locale = QLocale::system();
    if (label == key || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return favorite ? QString::fromUtf16(u"\u79FB\u9664\u6536\u85CF") : QString::fromUtf16(u"\u52A0\u5165\u6536\u85CF");
        }
        return key;
    }
    return label;
}

QString pinActionLabel(bool pinned) {
    const QString key = pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin to top");
    QString label = QObject::tr(key.toUtf8().constData());
    const QLocale locale = QLocale::system();
    if (label == key || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return pinned ? QString::fromUtf16(u"\u53D6\u6D88\u7F6E\u9876") : QString::fromUtf16(u"\u7F6E\u9876");
        }
        return key;
    }
    return label;
}

QString saveItemLabel() {
    QString label = QObject::tr("Save");
    if (label == QLatin1String("Save") || looksBrokenTranslation(label)) {
        return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58") : QStringLiteral("Save");
    }
    return label;
}

QString deleteLabel() {
    QString label = QObject::tr("Delete");
    if (label == QLatin1String("Delete") || looksBrokenTranslation(label)) {
        return prefersChineseUi() ? QString::fromUtf16(u"\u5220\u9664") : QStringLiteral("Delete");
    }
    return label;
}

QString deleteSelectedLabel() {
    QString label = QObject::tr("Delete Selected");
    if (label == QLatin1String("Delete Selected") || looksBrokenTranslation(label)) {
        return prefersChineseUi() ? QString::fromUtf16(u"\u5220\u9664\u6240\u9009") : QStringLiteral("Delete Selected");
    }
    return label;
}

QString saveDialogTitle(ClipboardItem::ContentType type) {
    switch (type) {
        case ClipboardItem::Image:
            return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58\u56FE\u7247") : QStringLiteral("Save Image");
        case ClipboardItem::RichText:
            return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58 HTML") : QStringLiteral("Save HTML");
        case ClipboardItem::Text:
            return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58\u6587\u672C") : QStringLiteral("Save Text");
        default:
            return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58") : QStringLiteral("Save");
    }
}

QString saveFailedTitle() {
    return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58\u5931\u8D25") : QStringLiteral("Save Failed");
}

QString saveFailedMessage(const QString &reason) {
    if (prefersChineseUi()) {
        return reason.isEmpty()
            ? QString::fromUtf16(u"\u4FDD\u5B58\u6587\u4EF6\u5931\u8D25\u3002")
            : QString::fromUtf16(u"\u4FDD\u5B58\u6587\u4EF6\u5931\u8D25\u3002\n%1").arg(reason);
    }
    return reason.isEmpty()
        ? QStringLiteral("Failed to save the file.")
        : QStringLiteral("Failed to save the file.\n%1").arg(reason);
}

bool supportsSaveToFile(ClipboardItem::ContentType type) {
    return type == ClipboardItem::Text
        || type == ClipboardItem::RichText
        || type == ClipboardItem::Image;
}

QString sanitizeExportBaseName(QString baseName) {
    static const QString invalidChars = QStringLiteral("<>:\"/\\|?*");
    for (QChar &ch : baseName) {
        if (ch.unicode() < 32 || invalidChars.contains(ch)) {
            ch = QLatin1Char(' ');
        }
    }

    baseName = baseName.simplified();
    while (!baseName.isEmpty()
           && (baseName.endsWith(QLatin1Char(' ')) || baseName.endsWith(QLatin1Char('.')))) {
        baseName.chop(1);
    }
    return baseName;
}

QString exportBaseNameForItem(const ClipboardItem &item) {
    QString baseName = item.getAlias().trimmed();
    if (baseName.isEmpty()) {
        baseName = item.getTitle().trimmed();
    }
    if (baseName.isEmpty()) {
        baseName = item.getNormalizedText().section(QLatin1Char('\n'), 0, 0).trimmed();
    }
    if (baseName.isEmpty()) {
        baseName = item.getName().trimmed();
    }

    baseName = sanitizeExportBaseName(baseName);
    if (baseName.size() > 64) {
        baseName = baseName.left(64).trimmed();
    }
    if (!baseName.isEmpty()) {
        return baseName;
    }

    const QDateTime timestamp = item.getTime().isValid() ? item.getTime() : QDateTime::currentDateTime();
    return QStringLiteral("clipboard-%1").arg(timestamp.toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

QString defaultExportDirectory() {
    const QString documentsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documentsDir.isEmpty() ? QDir::homePath() : documentsDir;
}

QString suggestedExportPath(const ClipboardItem &item, const QString &suffix) {
    return QDir(defaultExportDirectory()).filePath(exportBaseNameForItem(item) + suffix);
}

QString ensureFileSuffix(const QString &filePath, const QString &suffix) {
    if (filePath.isEmpty() || !QFileInfo(filePath).suffix().isEmpty()) {
        return filePath;
    }
    return filePath + suffix;
}

QString imageSaveFilters() {
    return QStringLiteral("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;BMP Image (*.bmp);;WebP Image (*.webp);;All Files (*)");
}

QString defaultImageSuffixForFilter(const QString &selectedFilter) {
    const QString lowerFilter = selectedFilter.toLower();
    if (lowerFilter.contains(QStringLiteral("*.jpg")) || lowerFilter.contains(QStringLiteral("*.jpeg"))) {
        return QStringLiteral(".jpg");
    }
    if (lowerFilter.contains(QStringLiteral("*.bmp"))) {
        return QStringLiteral(".bmp");
    }
    if (lowerFilter.contains(QStringLiteral("*.webp"))) {
        return QStringLiteral(".webp");
    }
    return QStringLiteral(".png");
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

    void setDark(bool dark) {
        dark_ = dark;
        update();
    }

    void setTransparentInset(int inset) {
        transparentInset_ = qMax(0, inset);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        const QPoint startPoint = side_ == Left ? rect().topLeft() : rect().topRight();
        const QPoint endPoint = side_ == Left ? rect().topRight() : rect().topLeft();
        QLinearGradient gradient(startPoint, endPoint);

        const QColor mistColor = dark_ ? QColor(22, 28, 36) : QColor(232, 236, 239);
        const int denseAlpha = dark_ ? 72 : 192;
        const int softAlpha = dark_ ? 40 : 104;
        const int faintAlpha = dark_ ? 18 : 36;
        QColor dense = mistColor;
        dense.setAlpha(denseAlpha);
        QColor soft = mistColor;
        soft.setAlpha(softAlpha);
        QColor faint = mistColor;
        faint.setAlpha(faintAlpha);
        QColor transparent = mistColor;
        transparent.setAlpha(0);

        const qreal width = qMax<qreal>(1.0, rect().width());
        const qreal insetRatio = qBound(0.0, transparentInset_ / width, 0.85);
        const qreal span = 1.0 - insetRatio;
        gradient.setColorAt(0.00, transparent);
        gradient.setColorAt(insetRatio, dense);
        gradient.setColorAt(insetRatio + span * 0.45, soft);
        gradient.setColorAt(insetRatio + span * 0.78, faint);
        gradient.setColorAt(1.00, transparent);
        painter.fillRect(rect(), gradient);
    }

private:
    Side side_;
    bool dark_ = false;
    int transparentInset_ = 0;
};

class ClipboardBoardView final : public QListView {
public:
    explicit ClipboardBoardView(const QString &debugLabel, QWidget *parent = nullptr)
        : QListView(parent),
          debugLabel_(debugLabel) {}

    void applyViewportMargins(int left, int top, int right, int bottom) {
        setViewportMargins(left, top, right, bottom);
    }

    void refreshItemGeometries() {
        updateGeometries();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        if (!fpsWindow_.isValid()) {
            resetPaintStats();
            fpsWindow_.start();
        }

        qint64 frameGapMs = 0;
        if (frameGapTimer_.isValid()) {
            frameGapMs = frameGapTimer_.restart();
        } else {
            frameGapTimer_.start();
        }

        QElapsedTimer paintTimer;
        paintTimer.start();
        QListView::paintEvent(event);
        const qint64 paintMs = paintTimer.elapsed();

        ++frameCount_;
        maxPaintMs_ = qMax(maxPaintMs_, paintMs);
        if (frameGapMs > 0) {
            maxFrameGapMs_ = qMax(maxFrameGapMs_, frameGapMs);
            if (frameGapMs > 20) {
                ++jank20Count_;
            }
            if (frameGapMs > 33) {
                ++jank33Count_;
            }
        }

        if (fpsWindow_.elapsed() >= 1000) {
            qInfo().noquote()
                << QStringLiteral("[board-fps] board=%1 fps=%2 maxFrameGapMs=%3 maxPaintMs=%4 jank>20ms=%5 jank>33ms=%6")
                      .arg(debugLabel_.isEmpty() ? objectName() : debugLabel_)
                      .arg(frameCount_)
                      .arg(maxFrameGapMs_)
                      .arg(maxPaintMs_)
                      .arg(jank20Count_)
                      .arg(jank33Count_);
            resetPaintStats();
            fpsWindow_.restart();
        }
    }

private:
    void resetPaintStats() {
        frameCount_ = 0;
        maxFrameGapMs_ = 0;
        maxPaintMs_ = 0;
        jank20Count_ = 0;
        jank33Count_ = 0;
        frameGapTimer_.invalidate();
    }

    QString debugLabel_;
    QElapsedTimer fpsWindow_;
    QElapsedTimer frameGapTimer_;
    int frameCount_ = 0;
    qint64 maxFrameGapMs_ = 0;
    qint64 maxPaintMs_ = 0;
    int jank20Count_ = 0;
    int jank33Count_ = 0;
};

}

ScrollItemsWidget::ScrollItemsWidget(const QString &category, const QString &borderColor, QWidget *parent)
    : QWidget(parent),
      ui(new Ui::ScrollItemsWidget),
      category(category),
      borderColor(borderColor),
      boardService_(new ClipboardBoardService(category, this)),
      scrollAnimation(nullptr) {
    ui->setupUi(this);

    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize itemOuterSize = cardOuterSizeForScale(scale);

    auto *hostLayout = new QVBoxLayout(ui->viewHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    auto *boardView = new ClipboardBoardView(category, ui->viewHost);
    listView_ = boardView;
    listView_->setObjectName(QStringLiteral("boardListView"));
    hostLayout->addWidget(listView_);

    boardModel_ = new ClipboardBoardModel(this);
    filterProxyModel_ = new ClipboardBoardProxyModel(this);
    filterProxyModel_->setSourceModel(boardModel_);
    proxyModel_ = new ClipboardBoardProxyModel(this);
    proxyModel_->setSourceModel(filterProxyModel_);
    proxyModel_->setPageSize(PAGE_SIZE);
    cardDelegate_ = new ClipboardCardDelegate(QColor::fromString(this->borderColor), listView_);

    listView_->setModel(proxyModel_);
    listView_->setItemDelegate(cardDelegate_);
    listView_->setViewMode(QListView::IconMode);
    listView_->setFlow(QListView::LeftToRight);
    listView_->setWrapping(false);
    listView_->setMovement(QListView::Static);
    listView_->setResizeMode(QListView::Adjust);
    listView_->setLayoutMode(QListView::SinglePass);
    listView_->setUniformItemSizes(true);
    listView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listView_->setSelectionBehavior(QAbstractItemView::SelectItems);
    listView_->setSelectionRectVisible(false);
    listView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    listView_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    listView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    listView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    listView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listView_->setFrameShape(QFrame::NoFrame);
    listView_->setContextMenuPolicy(Qt::CustomContextMenu);
    listView_->setMouseTracking(true);
    listView_->setSpacing(qMax(6, 8 * scale / 100));
    listView_->setGridSize(QSize(itemOuterSize.width() + listView_->spacing(), itemOuterSize.height()));
    listView_->setStyleSheet(QStringLiteral("QListView { background: transparent; border: none; }"));
    listView_->setAttribute(Qt::WA_TranslucentBackground);
    listView_->viewport()->setAttribute(Qt::WA_TranslucentBackground);

    loadingLabel_ = new QLabel(ui->viewHost);
    loadingLabel_->setObjectName(QStringLiteral("loadingOverlay"));
    loadingLabel_->setAlignment(Qt::AlignCenter);
    loadingLabel_->setWordWrap(true);
    loadingLabel_->setText(QStringLiteral("Loading...\n正在加载..."));
    loadingLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    loadingLabel_->hide();

    const int scrollbarHeight = qMax(12, listView_->horizontalScrollBar()->sizeHint().height());
    const int scrollHeight = itemOuterSize.height() + scrollbarHeight;
    edgeContentPadding_ = qMax(6, 8 * scale / 100);
    edgeFadeWidth_ = qMax(12, 16 * scale / 100);
    boardView->applyViewportMargins(edgeContentPadding_, 0, edgeContentPadding_, 0);
    listView_->setFixedHeight(scrollHeight);
    ui->viewHost->setFixedHeight(scrollHeight);
    setFixedHeight(scrollHeight);

    QScroller::grabGesture(listView_->viewport(), QScroller::TouchGesture);
    QScroller *scroller = QScroller::scroller(listView_->viewport());
    QScrollerProperties sp = scroller->scrollerProperties();
    sp.setScrollMetric(QScrollerProperties::FrameRate, QVariant::fromValue(QScrollerProperties::Fps60));
    sp.setScrollMetric(QScrollerProperties::DragStartDistance, 0.0025);
    sp.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                       QVariant::fromValue(QScrollerProperties::OvershootAlwaysOn));
    sp.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                       QVariant::fromValue(QScrollerProperties::OvershootAlwaysOn));
    scroller->setScrollerProperties(sp);

    listView_->horizontalScrollBar()->setSingleStep(48);
    scrollAnimation = new QPropertyAnimation(listView_->horizontalScrollBar(), "value", this);
    scrollAnimation->setEasingCurve(QEasingCurve::OutQuad);
    scrollAnimation->setDuration(70);

    connect(boardService_, &ClipboardBoardService::itemsLoaded, this, &ScrollItemsWidget::handleLoadedItems);
    connect(boardService_, &ClipboardBoardService::pendingItemReady, this, &ScrollItemsWidget::handlePendingItemReady);
    connect(boardService_, &ClipboardBoardService::thumbnailReady, this, &ScrollItemsWidget::handleThumbnailReady);
    connect(boardService_, &ClipboardBoardService::keywordMatched, this, &ScrollItemsWidget::handleKeywordMatched);
    connect(boardService_, &ClipboardBoardService::totalItemCountChanged, this, &ScrollItemsWidget::handleTotalItemCountChanged);
    connect(boardService_, &ClipboardBoardService::deferredLoadCompleted, this, &ScrollItemsWidget::handleDeferredLoadCompleted);
    connect(boardService_, &ClipboardBoardService::localPersistenceChanged, this, [this]() {
        emit localPersistenceChanged();
        if (!paginationEnabled()) {
            return;
        }
        loadedPage_ = -1;
        loadedPageBaseOffset_ = -1;
        loadedPageTotalItems_ = -1;
        if (boardService_) {
            syncPageWindow(false);
        }
        if (isBoardUiVisible()) {
            refreshContentWidthHint();
            primeVisibleThumbnailsSync();
            scheduleThumbnailUpdate();
        }
    });

    connect(listView_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &ScrollItemsWidget::handleCurrentIndexChanged);
    connect(listView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &, const QItemSelection &) {
                updateSelectionState();
            });
    connect(listView_, &QListView::doubleClicked, this, &ScrollItemsWidget::handleActivatedIndex);
    connect(listView_, &QListView::customContextMenuRequested, this, &ScrollItemsWidget::showContextMenu);
    connect(listView_->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) {
                maybeLoadMoreItems();
                scheduleThumbnailUpdate();
            });

    leftEdgeFadeOverlay_ = new EdgeFadeOverlay(EdgeFadeOverlay::Left, ui->viewHost);
    rightEdgeFadeOverlay_ = new EdgeFadeOverlay(EdgeFadeOverlay::Right, ui->viewHost);
    updateEdgeFadeOverlays();
    leftEdgeFadeOverlay_->show();
    rightEdgeFadeOverlay_->show();

    listView_->installEventFilter(this);
    listView_->viewport()->installEventFilter(this);

    createHoverActionBar();
    connect(listView_->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        updateHoverActionBarPosition();
    });

    thumbnailPulseTimer_ = new QTimer(this);
    thumbnailPulseTimer_->setInterval(220);
    connect(thumbnailPulseTimer_, &QTimer::timeout, this, [this]() {
        if (!cardDelegate_ || !listView_) {
            return;
        }
        thumbnailLoadingPhase_ = (thumbnailLoadingPhase_ + 7) % 100;
        cardDelegate_->setLoadingPhase(thumbnailLoadingPhase_);
        updateThumbnailViewport(visibleLoadingThumbnailNames_);
    });

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ScrollItemsWidget::applyTheme);
    updateLoadingOverlay();
}

ScrollItemsWidget::~ScrollItemsWidget() {
    delete ui;
}

void ScrollItemsWidget::applyTheme(bool dark) {
    darkTheme_ = dark;

    const int scale = MPasteSettings::getInst()->getItemScale();
    const int borderRadius = qMax(4, 6 * scale / 100);
    const int padding = qMax(2, 3 * scale / 100);
    const QString buttonStyle = hoverButtonStyle(darkTheme_, borderRadius, padding);

    if (hoverActionBar_) {
        auto *hoverBar = static_cast<HoverActionBar *>(hoverActionBar_);
        hoverBar->setColors(darkTheme_ ? QColor(26, 31, 38, 205) : QColor(255, 255, 255, 185),
                            darkTheme_ ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 120));
    }

    if (loadingLabel_) {
        const QString textColor = darkTheme_ ? QStringLiteral("#A7B2C4") : QStringLiteral("#6B7788");
        const QString glowColor = darkTheme_ ? QStringLiteral("rgba(31, 36, 45, 180)") : QStringLiteral("rgba(255, 255, 255, 180)");
        loadingLabel_->setStyleSheet(QStringLiteral("QLabel#loadingOverlay { color: %1; font-size: 16px; font-weight: 600; background: %2; border-radius: 12px; padding: 14px 18px; }")
                                         .arg(textColor, glowColor));
    }

    updateLoadingOverlay();

    if (hoverDetailsBtn_) {
        hoverDetailsBtn_->setIcon(IconResolver::themedIcon(QStringLiteral("details"), darkTheme_));
        hoverDetailsBtn_->setStyleSheet(buttonStyle);
    }
    if (hoverFavoriteBtn_) {
        hoverFavoriteBtn_->setStyleSheet(buttonStyle);
        bool favorite = false;
        if (hoverProxyIndex_.isValid() && proxyModel_ && boardModel_) {
            const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
            favorite = boardModel_->isFavorite(sourceRow);
        }
        updateHoverFavoriteButton(favorite);
    }
    if (hoverDeleteBtn_) {
        hoverDeleteBtn_->setStyleSheet(buttonStyle);
    }

    if (leftEdgeFadeOverlay_) {
        auto *overlay = static_cast<EdgeFadeOverlay *>(leftEdgeFadeOverlay_);
        overlay->setDark(darkTheme_);
    }
    if (rightEdgeFadeOverlay_) {
        auto *overlay = static_cast<EdgeFadeOverlay *>(rightEdgeFadeOverlay_);
        overlay->setDark(darkTheme_);
    }

    updateEdgeFadeOverlays();
    if (listView_) {
        listView_->viewport()->update();
    }
}

QModelIndex ScrollItemsWidget::currentProxyIndex() const {
    return listView_ ? listView_->currentIndex() : QModelIndex();
}

QModelIndex ScrollItemsWidget::proxyIndexForSourceRow(int sourceRow) const {
    if (!proxyModel_ || !filterProxyModel_ || !boardModel_ || sourceRow < 0) {
        return {};
    }

    const QModelIndex filterIndex = filterProxyModel_->mapFromSource(boardModel_->index(sourceRow, 0));
    if (!filterIndex.isValid()) {
        return {};
    }
    return proxyModel_->mapFromSource(filterIndex);
}

int ScrollItemsWidget::sourceRowForProxyIndex(const QModelIndex &proxyIndex) const {
    if (!proxyModel_ || !filterProxyModel_ || !proxyIndex.isValid()) {
        return -1;
    }

    const QModelIndex filterIndex = proxyModel_->mapToSource(proxyIndex);
    if (!filterIndex.isValid()) {
        return -1;
    }

    return filterProxyModel_->mapToSource(filterIndex).row();
}

QList<QModelIndex> ScrollItemsWidget::selectedProxyIndexes() const {
    QList<QModelIndex> result;
    if (!listView_ || !listView_->selectionModel()) {
        return result;
    }

    // selectedRows() requires all columns to be selected; under SelectItems
    // mode this may return empty even when items are visually selected.
    // Fall back to selectedIndexes() filtered to column 0.
    result = listView_->selectionModel()->selectedRows();
    if (result.isEmpty()) {
        for (const QModelIndex &idx : listView_->selectionModel()->selectedIndexes()) {
            if (idx.column() == 0) {
                result.append(idx);
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const QModelIndex &lhs, const QModelIndex &rhs) {
        return lhs.row() < rhs.row();
    });

    if (!result.isEmpty()) {
        return result;
    }

    const QModelIndex current = currentProxyIndex();
    if (current.isValid()) {
        result.append(current);
    }
    return result;
}

QList<int> ScrollItemsWidget::selectedSourceRows() const {
    QList<int> result;
    if (!proxyModel_) {
        return result;
    }

    for (const QModelIndex &proxyIndex : selectedProxyIndexes()) {
        if (!proxyIndex.isValid()) {
            continue;
        }
        const int sourceRow = sourceRowForProxyIndex(proxyIndex);
        if (sourceRow >= 0 && !result.contains(sourceRow)) {
            result.append(sourceRow);
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

QList<ClipboardItem> ScrollItemsWidget::selectedItems() const {
    QList<ClipboardItem> result;
    if (!boardModel_) {
        return result;
    }

    for (const int sourceRow : selectedSourceRows()) {
        const ClipboardItem item = boardModel_->itemAt(sourceRow);
        if (!item.getName().isEmpty()) {
            result.append(item);
        }
    }
    return result;
}

void ScrollItemsWidget::setCurrentProxyIndex(const QModelIndex &index) {
    if (!listView_ || !listView_->selectionModel()) {
        return;
    }

    if (!index.isValid()) {
        listView_->selectionModel()->clearCurrentIndex();
        return;
    }

    listView_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
}

void ScrollItemsWidget::handleCurrentIndexChanged(const QModelIndex &current, const QModelIndex &previous) {
    Q_UNUSED(current);
    Q_UNUSED(previous);

    ensureLinkPreviewForIndex(current);
    if (hoverActionBar_ && hoverActionBar_->isVisible()) {
        updateHoverActionBar(current);
    }
}

void ScrollItemsWidget::createHoverActionBar() {
    if (!listView_ || !ui || !ui->viewHost) {
        return;
    }

    const int scale = MPasteSettings::getInst()->getItemScale();
    const int barHeight = qMax(20, 28 * scale / 100);
    const int borderRadius = qMax(6, 8 * scale / 100);
    const int margin = qMax(4, 6 * scale / 100);
    const int spacing = qMax(2, 4 * scale / 100);
    const bool dark = ThemeManager::instance()->isDark();

    auto *hoverBar = new HoverActionBar(ui->viewHost);
    hoverActionBar_ = hoverBar;
    hoverActionBar_->setObjectName(QStringLiteral("cardActionBar"));
    hoverActionBar_->setFixedHeight(barHeight);
    hoverActionBar_->setFocusPolicy(Qt::NoFocus);
    hoverActionBar_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    hoverActionBar_->setAttribute(Qt::WA_TranslucentBackground);
    hoverActionBar_->setMouseTracking(true);
    hoverActionBar_->installEventFilter(this);
    hoverBar->setCornerRadius(borderRadius);
    hoverBar->setColors(dark ? QColor(26, 31, 38, 205) : QColor(255, 255, 255, 185),
                        dark ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 120));

    hoverOpacity_ = new QGraphicsOpacityEffect(hoverActionBar_);
    hoverOpacity_->setOpacity(0.0);
    hoverActionBar_->setGraphicsEffect(hoverOpacity_);

    auto *layout = new QHBoxLayout(hoverActionBar_);
    layout->setContentsMargins(margin, 0, margin, 0);
    layout->setSpacing(spacing);

    auto createButton = [this, scale, dark](const QString &iconPath, const QString &tooltip) {
        const int iconSz = qMax(12, 14 * scale / 100);
        const int btnSz = qMax(18, 22 * scale / 100);
        const int borderR = qMax(4, 6 * scale / 100);
        const int pad = qMax(2, 3 * scale / 100);

        auto *button = new QToolButton;
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(iconSz, iconSz));
        button->setFixedSize(btnSz, btnSz);
        button->setStyleSheet(hoverButtonStyle(dark, borderR, pad));
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tooltip);
        button->setFocusPolicy(Qt::NoFocus);
        button->setMouseTracking(true);
        button->installEventFilter(this);
        return button;
    };

    hoverDetailsBtn_ = createButton(IconResolver::themedPath(QStringLiteral("details"), dark), detailsLabel());
    hoverAliasBtn_ = createButton(IconResolver::themedPath(QStringLiteral("rename"), dark), aliasLabel());
    hoverPinBtn_ = createButton(IconResolver::themedPath(QStringLiteral("pin"), dark), pinActionLabel(false));
    hoverFavoriteBtn_ = createButton(IconResolver::themedPath(QStringLiteral("star_outline"), dark), favoriteActionLabel(false));
    hoverDeleteBtn_ = createButton(QStringLiteral(":/resources/resources/delete.svg"), deleteLabel());

    layout->addWidget(hoverDetailsBtn_);
    layout->addWidget(hoverAliasBtn_);
    layout->addWidget(hoverPinBtn_);
    layout->addWidget(hoverFavoriteBtn_);
    layout->addWidget(hoverDeleteBtn_);

    connect(hoverDetailsBtn_, &QToolButton::clicked, this, [this]() {
        if (!proxyModel_ || !boardModel_ || !hoverProxyIndex_.isValid()) {
            return;
        }
        const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item) {
            return;
        }
        const auto seq = displaySequenceForIndex(hoverProxyIndex_);
        emit detailsRequested(*item, seq.first, seq.second);
    });

    connect(hoverAliasBtn_, &QToolButton::clicked, this, [this]() {
        if (!proxyModel_ || !boardModel_ || !hoverProxyIndex_.isValid()) {
            return;
        }
        const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item) {
            return;
        }
        openAliasDialogForItem(*item);
        hideHoverActionBar(false);
    });

    connect(hoverPinBtn_, &QToolButton::clicked, this, [this]() {
        if (!proxyModel_ || !boardModel_ || !hoverProxyIndex_.isValid()) {
            return;
        }
        const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item) {
            return;
        }
        setItemPinned(*item, !item->isPinned());
        updateHoverPinButton(!item->isPinned());
    });

    connect(hoverFavoriteBtn_, &QToolButton::clicked, this, [this]() {
        if (!proxyModel_ || !boardModel_ || !hoverProxyIndex_.isValid()) {
            return;
        }
        const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item) {
            return;
        }
        const bool isFavorite = boardModel_->isFavorite(sourceRow);
        setItemFavorite(*item, !isFavorite);
        if (isFavorite) {
            emit itemUnstared(*item);
        } else {
            emit itemStared(*item);
        }
        updateHoverFavoriteButton(!isFavorite);
    });

    connect(hoverDeleteBtn_, &QToolButton::clicked, this, [this]() {
        if (!proxyModel_ || !boardModel_ || !hoverProxyIndex_.isValid()) {
            return;
        }
        const int sourceRow = sourceRowForProxyIndex(hoverProxyIndex_);
        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item) {
            return;
        }
        removeItemByContent(*item);
        hideHoverActionBar(false);
    });

    hoverActionBar_->hide();

    hoverHideTimer_ = new QTimer(this);
    hoverHideTimer_->setSingleShot(true);
    hoverHideTimer_->setInterval(80);
    connect(hoverHideTimer_, &QTimer::timeout, this, [this]() {
        hideHoverActionBar(true);
    });
}

void ScrollItemsWidget::updateHoverFavoriteButton(bool favorite) {
    if (!hoverFavoriteBtn_) {
        return;
    }
    if (favorite) {
        hoverFavoriteBtn_->setIcon(QIcon(QStringLiteral(":/resources/resources/star_filled.svg")));
    } else {
        hoverFavoriteBtn_->setIcon(IconResolver::themedIcon(QStringLiteral("star_outline"), darkTheme_));
    }
    hoverFavoriteBtn_->setToolTip(favoriteActionLabel(favorite));
}

void ScrollItemsWidget::updateHoverPinButton(bool pinned) {
    if (!hoverPinBtn_) {
        return;
    }
    hoverPinBtn_->setToolTip(pinActionLabel(pinned));
}

void ScrollItemsWidget::openAliasDialogForItem(const ClipboardItem &item) {
    if (!boardModel_) {
        return;
    }
    const int row = boardModel_->rowForName(item.getName());
    if (row < 0) {
        return;
    }
    ClipboardItemRenameDialog dialog(item.getAlias(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    ClipboardItem updated = boardModel_->itemAt(row);
    updated.setAlias(dialog.alias());
    boardModel_->updateItem(row, updated);
    if (boardService_) {
        ClipboardBoardActionService::persistItemMetadata(boardService_, boardModel_, row, updated);
    }
    emit aliasChanged(updated.fingerprint(), updated.getAlias());
}

void ScrollItemsWidget::saveItemToFile(const ClipboardItem &item) {
    const ClipboardItem::ContentType contentType = item.getContentType();
    if (!supportsSaveToFile(contentType)) {
        return;
    }

    QString selectedFilter;
    QString filePath;
    switch (contentType) {
        case ClipboardItem::Image:
            selectedFilter = QStringLiteral("PNG Image (*.png)");
            filePath = QFileDialog::getSaveFileName(this,
                                                    saveDialogTitle(contentType),
                                                    suggestedExportPath(item, QStringLiteral(".png")),
                                                    imageSaveFilters(),
                                                    &selectedFilter);
            filePath = ensureFileSuffix(filePath, defaultImageSuffixForFilter(selectedFilter));
            break;
        case ClipboardItem::RichText:
            filePath = QFileDialog::getSaveFileName(this,
                                                    saveDialogTitle(contentType),
                                                    suggestedExportPath(item, QStringLiteral(".html")),
                                                    QStringLiteral("HTML Files (*.html *.htm);;All Files (*)"));
            filePath = ensureFileSuffix(filePath, QStringLiteral(".html"));
            break;
        case ClipboardItem::Text:
            filePath = QFileDialog::getSaveFileName(this,
                                                    saveDialogTitle(contentType),
                                                    suggestedExportPath(item, QStringLiteral(".txt")),
                                                    QStringLiteral("Text Files (*.txt);;All Files (*)"));
            filePath = ensureFileSuffix(filePath, QStringLiteral(".txt"));
            break;
        default:
            return;
    }

    if (filePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    const bool success = ClipboardBoardActionService::exportItemToFile(item, filePath, &errorMessage);

    if (!success) {
        if (prefersChineseUi()) {
            if (errorMessage == QLatin1String("The current item does not contain savable HTML content.")) {
                errorMessage = QString::fromUtf16(u"\u5F53\u524D\u6761\u76EE\u6CA1\u6709\u53EF\u4FDD\u5B58\u7684 HTML \u5185\u5BB9\u3002");
            } else if (errorMessage == QLatin1String("Unable to write the image to the target file.")) {
                errorMessage = QString::fromUtf16(u"\u65E0\u6CD5\u5C06\u56FE\u50CF\u5199\u5165\u76EE\u6807\u6587\u4EF6\u3002");
            } else if (errorMessage == QLatin1String("The current item does not support export.")) {
                errorMessage = QString::fromUtf16(u"\u5F53\u524D\u6761\u76EE\u6682\u4E0D\u652F\u6301\u5BFC\u51FA\u3002");
            }
        }
        QMessageBox::warning(this, saveFailedTitle(), saveFailedMessage(errorMessage));
    }
}

void ScrollItemsWidget::setItemPinned(const ClipboardItem &item, bool pinned) {
    if (!boardModel_) {
        return;
    }
    const int row = boardModel_->rowForName(item.getName());
    if (row < 0) {
        return;
    }

    const ClipboardItem updated = boardModel_->itemAt(row);
    if (updated.isPinned() == pinned) {
        return;
    }

    if (shouldEvictPages()) {
        ClipboardItem persisted = updated;
        persisted.setPinned(pinned);
        boardModel_->updateItem(row, persisted);
        if (!ClipboardBoardActionService::persistItemMetadata(boardService_, boardModel_, row, persisted)) {
            return;
        }
        loadedPage_ = -1;
        loadedPageBaseOffset_ = -1;
        loadedPageTotalItems_ = -1;
        syncPageWindow(false);
        refreshContentWidthHint();
        updateHoverActionBar(currentProxyIndex());
        return;
    }

    const int targetRow = pinned ? 0 : unpinnedInsertRowForItem(updated, row);
    if (!ClipboardBoardActionService::applyPinnedState(boardModel_, boardService_, row, targetRow, pinned)) {
        return;
    }
    const QModelIndex targetIndex = proxyIndexForSourceRow(targetRow);
    setCurrentProxyIndex(targetIndex);
    updateHoverActionBar(targetIndex);
    refreshContentWidthHint();
}

void ScrollItemsWidget::updateHoverActionBar(const QModelIndex &proxyIndex) {
    if (!hoverActionBar_ || !listView_ || !proxyModel_ || !boardModel_) {
        return;
    }

    if (selectedItemCount() > 1) {
        hideHoverActionBar(false);
        return;
    }

    if (!proxyIndex.isValid()) {
        if (hoverHideTimer_ && !hoverHideTimer_->isActive()) {
            hoverHideTimer_->start();
        }
        return;
    }

    if (hoverHideTimer_ && hoverHideTimer_->isActive()) {
        hoverHideTimer_->stop();
    }

    const int sourceRow = sourceRowForProxyIndex(proxyIndex);
    const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
    if (!item || item->getName().isEmpty()) {
        if (hoverHideTimer_ && !hoverHideTimer_->isActive()) {
            hoverHideTimer_->start();
        }
        return;
    }

    hoverProxyIndex_ = proxyIndex;
    updateHoverFavoriteButton(boardModel_->isFavorite(sourceRow));
    updateHoverPinButton(item->isPinned());
    updateHoverActionBarPosition();

    if (!hoverActionBar_->isVisible()) {
        hoverActionBar_->show();
        hoverActionBar_->raise();
        if (hoverOpacity_) {
            auto *anim = new QPropertyAnimation(hoverOpacity_, "opacity", hoverActionBar_);
            anim->setDuration(50);
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
}

void ScrollItemsWidget::updateHoverActionBarPosition() {
    if (!hoverActionBar_ || !listView_ || !hoverProxyIndex_.isValid()) {
        return;
    }

    if (!listView_->model() || hoverProxyIndex_.model() != listView_->model()) {
        hideHoverActionBar(false);
        return;
    }

    QWidget *viewport = listView_->viewport();
    QWidget *overlayHost = hoverActionBar_->parentWidget();
    if (!viewport || !overlayHost) {
        hideHoverActionBar(false);
        return;
    }

    const QRect itemRect = listView_->visualRect(hoverProxyIndex_);
    const QRect viewportRect = viewport->rect();
    if (!itemRect.isValid() || !viewportRect.intersects(itemRect)) {
        hideHoverActionBar();
        return;
    }

    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize cardSize(kCardBaseWidth * scale / 100, kCardBaseHeight * scale / 100);
    const QRect cardRect(itemRect.topLeft(), cardSize);
    const QPoint hostTopLeft = viewport->mapTo(overlayHost, cardRect.topLeft());
    hoverActionBar_->adjustSize();
    if (overlayHost->width() <= 0 || overlayHost->height() <= 0
        || hoverActionBar_->width() <= 0 || hoverActionBar_->height() <= 0) {
        hideHoverActionBar(false);
        return;
    }

    const int desiredX = hostTopLeft.x() + (cardRect.width() - hoverActionBar_->width()) / 2;
    const int desiredY = hostTopLeft.y() + qMax(2, 4 * scale / 100);
    const int maxX = qMax(0, overlayHost->width() - hoverActionBar_->width());
    const int maxY = qMax(0, overlayHost->height() - hoverActionBar_->height());
    const int x = qBound(0, desiredX, maxX);
    const int y = qBound(0, desiredY, maxY);
    hoverActionBar_->move(x, y);
}

void ScrollItemsWidget::hideHoverActionBar(bool animated) {
    if (!hoverActionBar_ || !hoverActionBar_->isVisible()) {
        return;
    }
    if (hoverHideTimer_ && hoverHideTimer_->isActive()) {
        hoverHideTimer_->stop();
    }
    if (!animated || !hoverOpacity_) {
        hoverActionBar_->hide();
        return;
    }
    auto *anim = new QPropertyAnimation(hoverOpacity_, "opacity", hoverActionBar_);
    anim->setDuration(50);
    anim->setStartValue(hoverOpacity_->opacity());
    anim->setEndValue(0.0);
    connect(anim, &QPropertyAnimation::finished, hoverActionBar_, &QWidget::hide);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ScrollItemsWidget::ensureLinkPreviewForIndex(const QModelIndex &proxyIndex) {
    if (!proxyModel_ || !boardModel_ || !proxyIndex.isValid()) {
        return;
    }

    const int sourceRow = sourceRowForProxyIndex(proxyIndex);
    if (sourceRow < 0) {
        return;
    }

    ClipboardItem item = boardModel_->itemAt(sourceRow);
    if (item.getContentType() != ClipboardItem::Link) {
        return;
    }

    QString linkText = item.getUrl().trimmed();
    if (linkText.isEmpty()) {
        const QList<QUrl> urls = item.getNormalizedUrls();
        if (!urls.isEmpty()) {
            const QUrl &first = urls.first();
            linkText = first.isLocalFile() ? first.toLocalFile() : first.toString();
        }
    }
    if (linkText.isEmpty()) {
        linkText = item.getNormalizedText().left(512).trimmed();
    }
    if (linkText.isEmpty()) {
        return;
    }

    QUrl url(linkText);
    if (!url.isValid() || url.isRelative() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        return;
    }

    const QString requestKey = url.toString(QUrl::FullyEncoded);
    if (pendingLinkPreviewUrls_.contains(requestKey)) {
        return;
    }
    pendingLinkPreviewUrls_.insert(requestKey);

    const QByteArray fingerprint = item.fingerprint();
    auto *fetcher = new OpenGraphFetcher(url, this);
    connect(fetcher, &OpenGraphFetcher::finished, this, [this, fetcher, requestKey, fingerprint, linkText](const OpenGraphItem &ogItem) {
        pendingLinkPreviewUrls_.remove(requestKey);
        fetcher->deleteLater();

        if (ogItem.getImage().isNull() && ogItem.getTitle().isEmpty()) {
            return;
        }

        const int row = boardModel_->rowForFingerprint(fingerprint);
        if (row < 0) {
            return;
        }

        ClipboardItem updated = boardModel_->itemAt(row);

        // Icon/favicon may have been cleared by the memory cleanup cycle.
        // Reload from disk so the re-rendered card has the app icon.
        if (updated.getIcon().isNull() && boardService_) {
            const QString filePath = boardService_->filePathForName(updated.getName());
            if (!filePath.isEmpty()) {
                ClipboardItem diskItem = boardService_->loadItemLight(filePath, false);
                if (!diskItem.getIcon().isNull()) {
                    updated.setIcon(diskItem.getIcon());
                }
                if (updated.getFavicon().isNull() && !diskItem.getFavicon().isNull()) {
                    updated.setFavicon(diskItem.getFavicon());
                }
            }
        }

        if (!ogItem.getTitle().isEmpty()) {
            updated.setTitle(ogItem.getTitle());
        }
        if (!linkText.isEmpty()) {
            updated.setUrl(linkText);
        }
        if (!ogItem.getImage().isNull()) {
            if (ogItem.getImageKind() == OpenGraphItem::IconImage) {
                updated.setFavicon(ogItem.getImage());
            } else {
                updated.setThumbnail(ogItem.getImage());
            }
        }

        if (boardModel_->updateItem(row, updated)) {
            if (cardDelegate_) {
                cardDelegate_->invalidateCard(updated.getName());
            }
            if (boardService_) {
                boardService_->saveItem(updated);
            }
        }
    });
    fetcher->handle();
}

void ScrollItemsWidget::handleActivatedIndex(const QModelIndex &index) {
    setCurrentProxyIndex(index);
    const int sourceRow = sourceRowForProxyIndex(index);
    const ClipboardItem *selectedItem = selectedByEnter();
    if (selectedItem) {
        // Emit first so the handler can read the item before moveItemToFirst
        // (which reloads the model in evict mode and clears the cache).
        emit doubleClicked(*selectedItem);
        const bool alreadyFirst = (sourceRow == 0) && (currentPage_ == 0);
        if (!alreadyFirst) {
            moveItemToFirst(sourceRow);
        }
    }
}

void ScrollItemsWidget::showContextMenu(const QPoint &pos) {
    if (!listView_) {
        return;
    }

    QModelIndex proxyIndex = listView_->indexAt(pos);
    if (!proxyIndex.isValid()) {
        proxyIndex = currentProxyIndex();
    }
    if (!proxyIndex.isValid()) {
        return;
    }

    QItemSelectionModel *selectionModel = listView_->selectionModel();
    const bool preserveSelection = selectionModel
        && selectionModel->isSelected(proxyIndex)
        && selectionModel->selectedRows().size() > 1;
    if (preserveSelection) {
        selectionModel->setCurrentIndex(proxyIndex, QItemSelectionModel::NoUpdate);
    } else {
        setCurrentProxyIndex(proxyIndex);
    }

    const QList<ClipboardItem> selection = selectedItems();
    if (selection.isEmpty()) {
        return;
    }

    const ClipboardItem item = selection.first();
    if (item.getName().isEmpty()) {
        return;
    }

    const bool multiSelection = selection.size() > 1;

    QMenu menu(this);
    applyMenuTheme(&menu);
    if (multiSelection) {
        populateMultiSelectionMenu(&menu, selection);
    } else {
        populateSingleSelectionMenu(&menu, proxyIndex, item);
    }

    menu.exec(listView_->viewport()->mapToGlobal(pos));
}

void ScrollItemsWidget::populateMultiSelectionMenu(QMenu *menu, const QList<ClipboardItem> &selection) {
    if (!menu || selection.isEmpty() || !boardModel_) {
        return;
    }

    const QList<int> sourceRows = selectedSourceRows();
    bool hasFavorite = false;
    bool hasNonFavorite = false;
    for (const int sourceRow : sourceRows) {
        const bool favorite = boardModel_->isFavorite(sourceRow);
        hasFavorite = hasFavorite || favorite;
        hasNonFavorite = hasNonFavorite || !favorite;
    }

    if (hasNonFavorite) {
        menu->addAction(QIcon(QStringLiteral(":/resources/resources/star.svg")),
                        favoriteActionLabel(false),
                        [this, selection]() {
                            applyFavoriteToItems(selection, true);
                        });
    }
    if (hasFavorite) {
        menu->addAction(QIcon(QStringLiteral(":/resources/resources/star_filled.svg")),
                        favoriteActionLabel(true),
                        [this, selection]() {
                            applyFavoriteToItems(selection, false);
                        });
    }
    if (category != MPasteSettings::STAR_CATEGORY_NAME) {
        if (hasFavorite || hasNonFavorite) {
            menu->addSeparator();
        }
        menu->addAction(QIcon(QStringLiteral(":/resources/resources/delete.svg")),
                        deleteSelectedLabel(),
                        [this, selection]() {
                            removeItems(selection);
                        });
    }
}

void ScrollItemsWidget::populateSingleSelectionMenu(QMenu *menu, const QModelIndex &proxyIndex, const ClipboardItem &item) {
    if (!menu || !proxyIndex.isValid() || !proxyModel_ || !boardModel_) {
        return;
    }

    const bool dark = darkTheme_;
    const int sourceRow = sourceRowForProxyIndex(proxyIndex);
    const ClipboardItem::ContentType contentType = item.getContentType();
    const bool isFavorite = boardModel_->isFavorite(sourceRow);
    const QList<QUrl> urls = item.getNormalizedUrls();
    const bool canOpenFolder = contentType == ClipboardItem::File
        && !urls.isEmpty()
        && std::all_of(urls.begin(), urls.end(), [](const QUrl &url) { return url.isLocalFile(); });
    const bool canSaveToFile = supportsSaveToFile(contentType);

    menu->addAction(IconResolver::themedIcon(QStringLiteral("text_plain"), dark), plainTextPasteLabel(), [this]() {
        const ClipboardItem *selectedItem = selectedByEnter();
        if (selectedItem) {
            emit plainTextPasteRequested(*selectedItem);
        }
    });
    menu->addAction(IconResolver::themedIcon(QStringLiteral("details"), dark), detailsLabel(), [this, proxyIndex]() {
        setCurrentProxyIndex(proxyIndex);
        const ClipboardItem *selectedItem = currentSelectedItem();
        if (!selectedItem) {
            return;
        }
        const QPair<int, int> sequenceInfo = displaySequenceForIndex(proxyIndex);
        emit detailsRequested(*selectedItem, sequenceInfo.first, sequenceInfo.second);
    });
    menu->addAction(IconResolver::themedIcon(QStringLiteral("rename"), dark), aliasLabel(), [this, item]() {
        openAliasDialogForItem(item);
    });
    if (ClipboardItemPreviewDialog::supportsPreview(item)) {
        menu->addAction(IconResolver::themedIcon(QStringLiteral("preview"), dark), tr("Preview"), [this]() {
            const ClipboardItem *selectedItem = currentSelectedItem();
            if (selectedItem) {
                emit previewRequested(*selectedItem);
            }
        });
    }
    if (canSaveToFile) {
        menu->addAction(QIcon(QStringLiteral(":/resources/resources/save_black.svg")), saveItemLabel(), [this, proxyIndex]() {
            setCurrentProxyIndex(proxyIndex);
            const ClipboardItem *selectedItem = currentSelectedItem();
            if (selectedItem) {
                saveItemToFile(*selectedItem);
            }
        });
    }
    if (canOpenFolder) {
        menu->addAction(QIcon(QStringLiteral(":/resources/resources/files.svg")), openContainingFolderLabel(), [urls]() {
            PlatformRelated::revealInFileManager(urls);
        });
    }

    const bool isPinned = item.isPinned();
    menu->addAction(IconResolver::themedIcon(QStringLiteral("pin"), dark),
                    pinActionLabel(isPinned),
                    [this, item, isPinned]() {
                        setItemPinned(item, !isPinned);
                    });

    menu->addSeparator();
    menu->addAction(QIcon(isFavorite
                          ? QStringLiteral(":/resources/resources/star_filled.svg")
                          : QStringLiteral(":/resources/resources/star.svg")),
                    favoriteActionLabel(isFavorite),
                    [this, item, isFavorite]() {
                        if (isFavorite) {
                            setItemFavorite(item, false);
                            emit itemUnstared(item);
                        } else {
                            setItemFavorite(item, true);
                            emit itemStared(item);
                        }
                    });
    menu->addAction(QIcon(QStringLiteral(":/resources/resources/delete.svg")), deleteLabel(), [this, item]() {
        if (category == MPasteSettings::STAR_CATEGORY_NAME) {
            emit itemUnstared(item);
            return;
        }
        removeItemByContent(item);
    });
}

void ScrollItemsWidget::handleLoadedItems(const QList<QPair<QString, ClipboardItem>> &items) {
    if (items.isEmpty() || !boardModel_) {
        return;
    }

    for (const auto &payload : items) {
        appendLoadedItem(payload.first, payload.second);
    }

    if (shouldEvictPages()) {
        reloadCurrentPageItems(false);
    }

    emit itemCountChanged(itemCountForDisplay());
    if (isBoardUiVisible()) {
        setFirstVisibleItemSelected();
        updateContentWidthHint();
        scheduleThumbnailUpdate();
    }
    updateLoadingOverlay();
}

void ScrollItemsWidget::handlePendingItemReady(const QString &expectedName, const ClipboardItem &item) {
    if (!boardModel_ || expectedName.isEmpty()) {
        return;
    }

    pendingThumbnailNames_.remove(expectedName);
    if (item.hasThumbnail()) {
        missingThumbnailNames_.remove(expectedName);
    }
    const int row = boardModel_->rowForName(expectedName);
    if (row >= 0) {
        ClipboardItem updated = item;
        if (!filterShowsVisualPreviewCards() && usesManagedVisualPreviewCard(updated)) {
            updated.setThumbnail(QPixmap());
        }
        boardModel_->updateItem(row, updated);
        syncPreviewStateForRow(row);
    }
    // The async save may have changed the service index count; keep
    // bookkeeping in sync to avoid a spurious full page reload.
    if (paginationEnabled() && loadedPageTotalItems_ >= 0) {
        loadedPageTotalItems_ = totalItemCountForPagination();
    }
    scheduleThumbnailUpdate();
    updateLoadingOverlay();
}

void ScrollItemsWidget::handleThumbnailReady(const QString &expectedName, const QPixmap &thumbnail) {
    pendingThumbnailNames_.remove(expectedName);
    if (!desiredThumbnailNames_.contains(expectedName)) {
        return;
    }
    if (!boardModel_) {
        return;
    }

    const int row = boardModel_->rowForName(expectedName);
    if (row < 0) {
        return;
    }

    const ClipboardItem *itemPtr = boardModel_->itemPtrAt(row);
    if (!itemPtr || !shouldManageThumbnail(*itemPtr)) {
        return;
    }
    if (!filterShowsVisualPreviewCards()) {
        syncPreviewStateForRow(row);
        return;
    }
    if (!thumbnail.isNull()) {
        missingThumbnailNames_.remove(expectedName);
        // Update thumbnail in-place, single dataChanged signal.
        ClipboardItem updated = *itemPtr;
        updated.setThumbnail(thumbnail);
        boardModel_->updateItem(row, updated);
    } else {
        missingThumbnailNames_.insert(expectedName);
    }
    syncPreviewStateForRow(row);

    scheduleThumbnailUpdate();
}

void ScrollItemsWidget::handleKeywordMatched(const QSet<QString> &matchedNames, quint64 token) {
    if (token != keywordSearchToken_) {
        return;
    }
    asyncKeywordMatchedNames_ = matchedNames;
    applyFilters();
}

void ScrollItemsWidget::handleTotalItemCountChanged(int total) {
    Q_UNUSED(total);
    syncPageWindow(false);
    emit itemCountChanged(itemCountForDisplay());
}

void ScrollItemsWidget::handleDeferredLoadCompleted() {
    // Pre-render all cards into cardPixmapCache_ immediately so the first
    // window show is instant.  Then release intermediate data.
    preRenderAndCleanup();

    if (isBoardUiVisible()) {
        refreshContentWidthHint();
    }
    updateLoadingOverlay();
}

void ScrollItemsWidget::setFirstVisibleItemSelected() {
    if (!proxyModel_ || !listView_) {
        return;
    }

    if (listView_->selectionModel() && !listView_->selectionModel()->selectedRows().isEmpty()) {
        return;
    }

    const QModelIndex current = currentProxyIndex();
    if (current.isValid() && current.model() == proxyModel_) {
        return;
    }

    if (proxyModel_->rowCount() > 0) {
        setCurrentProxyIndex(proxyModel_->index(0, 0));
    } else if (listView_->selectionModel()) {
        listView_->selectionModel()->clearCurrentIndex();
    }
}

int ScrollItemsWidget::totalItemCountForPagination() const {
    if (paginationEnabled()) {
        return boardService_
            ? boardService_->filteredItemCount(currentTypeFilter_, currentKeyword_, asyncKeywordMatchedNames_)
            : 0;
    }
    if (!currentKeyword_.isEmpty() || currentTypeFilter_ != ClipboardItem::All) {
        return filterProxyModel_ ? filterProxyModel_->rowCount() : 0;
    }
    return boardService_ ? boardService_->totalItemCount()
                         : (filterProxyModel_ ? filterProxyModel_->rowCount() : 0);
}

bool ScrollItemsWidget::paginationEnabled() const {
    return MPasteSettings::getInst()->getHistoryViewMode() == MPasteSettings::ViewModePaged;
}

bool ScrollItemsWidget::shouldEvictPages() const {
    return paginationEnabled();
}

bool ScrollItemsWidget::filterShowsVisualPreviewCards() const {
    return currentTypeFilter_ == ClipboardItem::All
        || currentTypeFilter_ == ClipboardItem::Image
        || currentTypeFilter_ == ClipboardItem::Link
        || currentTypeFilter_ == ClipboardItem::Office
        || currentTypeFilter_ == ClipboardItem::RichText;
}

bool ScrollItemsWidget::usesManagedVisualPreviewCard(const ClipboardItem &item) const {
    const ClipboardItem::ContentType type = item.getContentType();
    return type == ClipboardItem::Image
        || type == ClipboardItem::Office
        || (type == ClipboardItem::RichText && item.getPreviewKind() == ClipboardItem::VisualPreview);
}

ClipboardBoardModel::PreviewState ScrollItemsWidget::previewStateForItem(const ClipboardItem &item) const {
    if (!usesManagedVisualPreviewCard(item)) {
        return ClipboardBoardModel::PreviewNotApplicable;
    }
    if (item.hasThumbnail()) {
        return ClipboardBoardModel::PreviewReady;
    }
    if (missingThumbnailNames_.contains(item.getName())) {
        return ClipboardBoardModel::PreviewUnavailable;
    }
    return ClipboardBoardModel::PreviewLoading;
}

void ScrollItemsWidget::syncPreviewStateForRow(int row) {
    if (!boardModel_ || row < 0) {
        return;
    }
    boardModel_->setPreviewState(row, previewStateForItem(boardModel_->itemAt(row)));
}

void ScrollItemsWidget::syncPreviewStateForName(const QString &name) {
    if (!boardModel_ || name.isEmpty()) {
        return;
    }
    syncPreviewStateForRow(boardModel_->rowForName(name));
}

void ScrollItemsWidget::releaseManagedVisualThumbnailsFromModel() {
    if (!boardModel_) {
        return;
    }

    pendingThumbnailNames_.clear();
    desiredThumbnailNames_.clear();
    managedThumbnailNames_.clear();
    visibleLoadingThumbnailNames_.clear();

    const int rowCount = boardModel_->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        ClipboardItem item = boardModel_->itemAt(row);
        if (!usesManagedVisualPreviewCard(item)) {
            continue;
        }
        if (item.hasThumbnail()) {
            item.setThumbnail(QPixmap());
            boardModel_->updateItem(row, item);
        }
        syncPreviewStateForRow(row);
    }
}

void ScrollItemsWidget::reloadAllIndexedItems() {
    if (!boardService_ || !boardModel_) {
        return;
    }

    if (paginationEnabled() && cardDelegate_) {
        cardDelegate_->clearVisualCaches();
    }

    const QList<QPair<QString, ClipboardItem>> items =
        boardService_->loadIndexedSlice(0, boardService_->totalItemCount());

    pendingThumbnailNames_.clear();
    missingThumbnailNames_.clear();
    desiredThumbnailNames_.clear();
    managedThumbnailNames_.clear();
    visibleLoadingThumbnailNames_.clear();
    selectedItemCache_ = ClipboardItem();
    pageBaseOffset_ = 0;
    loadedPage_ = -1;
    loadedPageBaseOffset_ = -1;
    loadedPageTotalItems_ = -1;
    loadedPageTypeFilter_ = ClipboardItem::All;
    loadedPageKeyword_.clear();
    loadedPageMatchedNames_.clear();
    boardModel_->clear();

    for (const auto &payload : items) {
        appendModelItem(payload.second);
    }
}

void ScrollItemsWidget::reloadCurrentPageItems(bool resetSelection) {
    if (!boardService_ || !boardModel_) {
        return;
    }

    QString previousSelectedName;
    if (!resetSelection) {
        if (const ClipboardItem *selectedItem = currentSelectedItem()) {
            previousSelectedName = selectedItem->getName();
        }
    }

    if (cardDelegate_) {
        cardDelegate_->clearIntermediateCaches();
    }

    const int totalItems = totalItemCountForPagination();
    const int totalPages = totalItems > 0 ? ((totalItems + PAGE_SIZE - 1) / PAGE_SIZE) : 0;
    const int clampedPage = totalPages > 0 ? qBound(0, currentPage_, totalPages - 1) : 0;
    currentPage_ = clampedPage;
    pageBaseOffset_ = totalPages > 0 ? currentPage_ * PAGE_SIZE : 0;

    // Load with thumbnails so cards can render immediately without
    // waiting for the async thumbnail management cycle.
    const QList<QPair<QString, ClipboardItem>> items =
        boardService_->loadFilteredIndexedSlice(currentTypeFilter_,
                                                currentKeyword_,
                                                asyncKeywordMatchedNames_,
                                                pageBaseOffset_,
                                                PAGE_SIZE,
                                                true);

    pendingThumbnailNames_.clear();
    missingThumbnailNames_.clear();
    desiredThumbnailNames_.clear();
    managedThumbnailNames_.clear();
    visibleLoadingThumbnailNames_.clear();
    selectedItemCache_ = ClipboardItem();
    boardModel_->clear();

    for (const auto &payload : items) {
        appendModelItem(payload.second);
        // Register loaded thumbnails so updateVisibleThumbnails can
        // properly compute the leaving set and unload distant ones.
        if (payload.second.hasThumbnail() && shouldManageThumbnail(payload.second)) {
            managedThumbnailNames_.insert(payload.second.getName());
        }
    }

    loadedPage_ = currentPage_;
    loadedPageBaseOffset_ = pageBaseOffset_;
    loadedPageTotalItems_ = totalItems;
    loadedPageTypeFilter_ = currentTypeFilter_;
    loadedPageKeyword_ = currentKeyword_;
    loadedPageMatchedNames_ = asyncKeywordMatchedNames_;

    if (listView_) {
        if (!resetSelection && !previousSelectedName.isEmpty()) {
            const int selectedRow = boardModel_->rowForName(previousSelectedName);
            if (selectedRow >= 0) {
                setCurrentProxyIndex(proxyIndexForSourceRow(selectedRow));
                return;
            }
        }

        if (resetSelection) {
            setFirstVisibleItemSelected();
            QScrollBar *scrollBar = horizontalScrollbar();
            if (scrollBar) {
                scrollBar->setValue(scrollBar->minimum());
            }
        } else if (listView_->selectionModel()) {
            listView_->selectionModel()->clearCurrentIndex();
        }
    }
}

bool ScrollItemsWidget::shouldReloadCurrentPage(int totalItems) const {
    if (!paginationEnabled() || !boardModel_) {
        return false;
    }

    const int totalPages = totalItems > 0 ? ((totalItems + PAGE_SIZE - 1) / PAGE_SIZE) : 0;
    const int clampedPage = totalPages > 0 ? qBound(0, currentPage_, totalPages - 1) : 0;
    const int targetOffset = totalPages > 0 ? clampedPage * PAGE_SIZE : 0;
    const int targetCount = qMin(PAGE_SIZE, qMax(0, totalItems - targetOffset));

    return loadedPage_ != clampedPage
        || loadedPageBaseOffset_ != targetOffset
        || loadedPageTypeFilter_ != currentTypeFilter_
        || loadedPageKeyword_ != currentKeyword_
        || loadedPageMatchedNames_ != asyncKeywordMatchedNames_
        || boardModel_->rowCount() != targetCount;
}

void ScrollItemsWidget::ensureCurrentPageLoaded(bool resetSelection) {
    if (!paginationEnabled() || !boardService_ || !boardModel_) {
        return;
    }

    if (shouldEvictPages()) {
        const int totalItems = totalItemCountForPagination();
        if (shouldReloadCurrentPage(totalItems)) {
            reloadCurrentPageItems(resetSelection);
        } else if (loadedPageTotalItems_ != totalItems) {
            // Total count changed but current page content is identical —
            // just update the cached count to avoid a full reload.
            loadedPageTotalItems_ = totalItems;
        }
        return;
    }

    const int totalItems = totalItemCountForPagination();
    const int requiredCount = qMin(totalItems, (currentPage_ + 1) * PAGE_SIZE);
    while (boardModel_->rowCount() < requiredCount && boardService_->hasPendingItems()) {
        boardService_->loadNextBatch(PAGE_LOAD_BATCH_SIZE);
    }
}

void ScrollItemsWidget::syncPageWindow(bool resetSelection) {
    if (!proxyModel_) {
        return;
    }

    if (!paginationEnabled()) {
        currentPage_ = 0;
        pageBaseOffset_ = 0;
        loadedPage_ = -1;
        loadedPageBaseOffset_ = -1;
        loadedPageTotalItems_ = -1;
        loadedPageTypeFilter_ = ClipboardItem::All;
        loadedPageKeyword_.clear();
        loadedPageMatchedNames_.clear();
        proxyModel_->setPageSize(0);
        proxyModel_->setPageIndex(0);
        if (listView_ && resetSelection) {
            setFirstVisibleItemSelected();
        }
        emit pageStateChanged(0, 0);
        return;
    }

    const int totalItems = totalItemCountForPagination();
    const int totalPages = totalItems > 0 ? ((totalItems + PAGE_SIZE - 1) / PAGE_SIZE) : 0;
    const int clampedPage = totalPages > 0 ? qBound(0, currentPage_, totalPages - 1) : 0;
    if (currentPage_ != clampedPage) {
        currentPage_ = clampedPage;
    }

    if (!shouldEvictPages()) {
        pageBaseOffset_ = 0;
    }

    proxyModel_->setPageSize(shouldEvictPages() ? 0 : PAGE_SIZE);
    ensureCurrentPageLoaded(resetSelection);
    proxyModel_->setPageIndex(shouldEvictPages() ? 0 : currentPage_);

    if (listView_ && resetSelection) {
        setFirstVisibleItemSelected();
        QScrollBar *scrollBar = horizontalScrollbar();
        if (scrollBar) {
            scrollBar->setValue(scrollBar->minimum());
        }
    }

    emit pageStateChanged(currentPageNumber(), totalPages);
}

void ScrollItemsWidget::applyFilters() {
    if (!filterShowsVisualPreviewCards()) {
        releaseManagedVisualThumbnailsFromModel();
    }
    if (!filterShowsVisualPreviewCards() && cardDelegate_) {
        cardDelegate_->clearVisualCaches();
    }

    if (!paginationEnabled()
        && (!currentKeyword_.isEmpty() || currentTypeFilter_ != ClipboardItem::All)
        && boardService_) {
        if (boardService_->hasPendingItems()) {
            ensureAllItemsLoaded();
        }
    }

    if (filterProxyModel_) {
        filterProxyModel_->setTypeFilter(paginationEnabled() ? ClipboardItem::All : currentTypeFilter_);
        filterProxyModel_->setKeyword(paginationEnabled() ? QString() : currentKeyword_);
        filterProxyModel_->setAsyncMatchedNames(paginationEnabled() ? QSet<QString>() : asyncKeywordMatchedNames_);
    }

    currentPage_ = 0;
    syncPageWindow(true);
    if (isBoardUiVisible()) {
        refreshContentWidthHint();
        primeVisibleThumbnailsSync();
        scheduleThumbnailUpdate();
    }
    emit itemCountChanged(itemCountForDisplay());
}

void ScrollItemsWidget::updateSelectionState() {
    if (selectedItemCount() > 1) {
        hideHoverActionBar(false);
    }
    emit selectionStateChanged();
}

int ScrollItemsWidget::moveItemToFirst(int sourceRow) {
    if (sourceRow < 0 || !boardModel_) {
        return sourceRow;
    }

    ClipboardItem item = boardModel_->itemAt(sourceRow);
    if (item.getName().isEmpty()) {
        return sourceRow;
    }

    if (shouldEvictPages()) {
        // In evict mode the model only holds the current page.  Move the
        // entry to the front of the service index.  If we are already on
        // page 0 the item is in the model so we can just move the row;
        // otherwise switch to page 1 which triggers a reload.
        if (boardService_) {
            boardService_->moveIndexedItemToFront(item.getName());
        }
        if (currentPage_ == 0) {
            const int targetRow = item.isPinned() ? 0 : pinnedInsertRow();
            if (targetRow != sourceRow) {
                boardModel_->moveItemToRow(sourceRow, targetRow);
            }
            setCurrentProxyIndex(proxyIndexForSourceRow(targetRow));
            return targetRow;
        }
        currentPage_ = 0;
        reloadCurrentPageItems(true);
        syncPageWindow(true);
        return 0;
    }

    const int targetRow = item.isPinned() ? 0 : pinnedInsertRow();
    if (targetRow != sourceRow) {
        boardModel_->moveItemToRow(sourceRow, targetRow);
    }

    if (paginationEnabled() && currentPage_ != 0) {
        setCurrentPageNumber(1);
    }

    setCurrentProxyIndex(proxyIndexForSourceRow(targetRow));
    return targetRow;
}

void ScrollItemsWidget::animateScrollTo(int targetValue) {
    QScrollBar *scrollBar = horizontalScrollbar();
    if (!scrollBar) {
        return;
    }

    targetValue = qBound(scrollBar->minimum(), targetValue, scrollBar->maximum());
    if (scrollBar->value() == targetValue) {
        return;
    }

    scrollAnimation->stop();
    const int distance = qAbs(targetValue - scrollBar->value());
    scrollAnimation->setDuration(qBound(28, 24 + distance / 12, 70));
    scrollAnimation->setStartValue(scrollBar->value());
    scrollAnimation->setEndValue(targetValue);
    scrollAnimation->start();
}

int ScrollItemsWidget::wheelStepPixels() const {
    const int viewportWidth = listView_ && listView_->viewport() ? listView_->viewport()->width() : 0;
    const int itemWidth = cardOuterSizeForScale(MPasteSettings::getInst()->getItemScale()).width();
    const int viewportStep = viewportWidth > 0 ? (viewportWidth * 2) / 5 : 480;
    const int itemStep = itemWidth > 0 ? (itemWidth * 3) / 2 : 0;
    const int scrollbarStep = listView_ ? listView_->horizontalScrollBar()->singleStep() * 8 : 0;
    return qMax(scrollbarStep, qMax(viewportStep, itemStep));
}

void ScrollItemsWidget::ensureAllItemsLoaded() {
    if (!boardService_) {
        return;
    }
    boardService_->ensureAllItemsLoaded(LOAD_BATCH_SIZE);
}

void ScrollItemsWidget::maybeLoadMoreItems() {
    if (paginationEnabled() || !boardService_ || boardService_->deferredLoadActive() || !boardService_->hasPendingItems()
        || !currentKeyword_.isEmpty() || currentTypeFilter_ != ClipboardItem::All) {
        return;
    }

    QScrollBar *scrollBar = horizontalScrollbar();
    if (!scrollBar) {
        return;
    }

    while (boardService_->hasPendingItems()) {
        const int remaining = scrollBar->maximum() - scrollBar->value();
        if (scrollBar->maximum() > 0 && remaining > LOAD_MORE_THRESHOLD_PX) {
            break;
        }

        const int pendingBefore = boardService_->pendingCount();
        boardService_->loadNextBatch(LOAD_BATCH_SIZE);
        if (boardService_->pendingCount() == pendingBefore || scrollBar->maximum() > 0) {
            break;
        }
    }
}

int ScrollItemsWidget::itemCountForDisplay() const {
    return totalItemCountForPagination();
}

void ScrollItemsWidget::trimExpiredItems() {
    if (category == MPasteSettings::STAR_CATEGORY_NAME || !boardService_) {
        return;
    }
    setFirstVisibleItemSelected();
}

void ScrollItemsWidget::prepareLoadFromSaveDir() {
    if (!boardModel_ || !boardService_) {
        return;
    }

    boardModel_->clear();
    selectedItemCache_ = ClipboardItem();
    asyncKeywordMatchedNames_.clear();
    missingThumbnailNames_.clear();
    currentPage_ = 0;
    pageBaseOffset_ = 0;
    loadedPage_ = -1;
    loadedPageBaseOffset_ = -1;
    loadedPageTotalItems_ = -1;
    loadedPageTypeFilter_ = ClipboardItem::All;
    loadedPageKeyword_.clear();
    loadedPageMatchedNames_.clear();
    if (proxyModel_) {
        proxyModel_->setPageIndex(0);
    }
    boardService_->refreshIndex();
    updateContentWidthHint();
    updateLoadingOverlay();
    emit pageStateChanged(currentPageNumber(), totalPageCount());
}

bool ScrollItemsWidget::shouldKeepDeferredLoading() const {
    if (!boardService_ || !boardService_->hasPendingItems() || !currentKeyword_.isEmpty()
        || currentTypeFilter_ != ClipboardItem::All) {
        return false;
    }

    QScrollBar *scrollBar = horizontalScrollbar();
    if (!scrollBar) {
        return false;
    }

    const int remaining = scrollBar->maximum() - scrollBar->value();
    return scrollBar->maximum() <= 0 || remaining <= LOAD_MORE_THRESHOLD_PX;
}

void ScrollItemsWidget::refreshContentWidthHint() {
    updateContentWidthHint();
    updateEdgeFadeOverlays();

    QScrollBar *scrollBar = horizontalScrollbar();
    if (scrollBar) {
        scrollBar->setValue(qMin(scrollBar->value(), scrollBar->maximum()));
    }
}

void ScrollItemsWidget::updateLoadingOverlay() {
    if (!loadingLabel_ || !ui || !ui->viewHost) {
        return;
    }

    const bool hasItems = boardModel_ && boardModel_->rowCount() > 0;
    const bool isLoading = boardService_ && (boardService_->hasPendingItems() || boardService_->deferredLoadActive());
    const bool shouldShow = isLoading && !hasItems;
    loadingLabel_->setVisible(shouldShow);
    if (shouldShow) {
        const QRect hostRect = ui->viewHost->rect();
        const QSize labelSize(qMin(hostRect.width(), 260), qMin(hostRect.height(), 80));
        const QPoint topLeft((hostRect.width() - labelSize.width()) / 2,
                             (hostRect.height() - labelSize.height()) / 2);
        loadingLabel_->setGeometry(QRect(topLeft, labelSize));
        loadingLabel_->raise();
    }
}

void ScrollItemsWidget::scheduleThumbnailUpdate() {
    // No-op: cardPixmapCache_ manages all rendering.
}

void ScrollItemsWidget::primeVisibleThumbnailsSync() {
    if (!boardService_ || !boardModel_ || !proxyModel_ || !listView_ || !isBoardUiVisible()) {
        return;
    }

    const int proxyCount = proxyModel_->rowCount();
    if (proxyCount <= 0) {
        return;
    }

    const QRect viewportRect = listView_->viewport()->rect();
    const int gridWidth = qMax(1, listView_->gridSize().width());
    const int visibleCount = qMax(1, viewportRect.width() / gridWidth + 2);
    const QModelIndex leftIndex = listView_->indexAt(QPoint(viewportRect.left() + 1, viewportRect.center().y()));
    const int startRow = leftIndex.isValid() ? leftIndex.row() : 0;
    const int endRow = qMin(proxyCount - 1, startRow + visibleCount - 1);

    for (int proxyRow = startRow; proxyRow <= endRow; ++proxyRow) {
        const QModelIndex proxyIndex = proxyModel_->index(proxyRow, 0);
        const int sourceRow = sourceRowForProxyIndex(proxyIndex);
        if (sourceRow < 0) {
            continue;
        }

        const ClipboardItem *item = boardModel_->itemPtrAt(sourceRow);
        if (!item || !shouldManageThumbnail(*item) || item->hasThumbnail() || !item->hasThumbnailHint()) {
            continue;
        }

        const QString filePath = item->sourceFilePath().isEmpty()
            ? boardService_->filePathForName(item->getName())
            : item->sourceFilePath();
        if (filePath.isEmpty()) {
            continue;
        }

        const ClipboardItem loaded = boardService_->loadItemLight(filePath, true);
        if (!loaded.getName().isEmpty() && loaded.hasThumbnail()) {
            boardModel_->updateItem(sourceRow, loaded);
            syncPreviewStateForRow(sourceRow);
        }
    }
}

bool ScrollItemsWidget::shouldManageThumbnail(const ClipboardItem &item) const {
    const ClipboardItem::ContentType type = item.getContentType();
    return type == ClipboardItem::Image
        || type == ClipboardItem::Link
        || type == ClipboardItem::Office
        || (type == ClipboardItem::RichText && item.getPreviewKind() == ClipboardItem::VisualPreview);
}

void ScrollItemsWidget::requestThumbnailForItem(const ClipboardItem &item) {
    if (!boardService_ || item.getName().isEmpty() || item.sourceFilePath().isEmpty()) {
        return;
    }
    if (missingThumbnailNames_.contains(item.getName())) {
        return;
    }
    if (pendingThumbnailNames_.contains(item.getName())) {
        return;
    }
    pendingThumbnailNames_.insert(item.getName());
    syncPreviewStateForName(item.getName());
    boardService_->requestThumbnailAsync(item.getName(), item.sourceFilePath());
}

void ScrollItemsWidget::applyManagedThumbnailNames(const QSet<QString> &desiredNames) {
    if (!boardModel_) {
        managedThumbnailNames_.clear();
        return;
    }

    QSet<QString> leavingNames = managedThumbnailNames_;
    leavingNames.subtract(desiredNames);

    for (const QString &name : leavingNames) {
        const int row = boardModel_->rowForName(name);
        if (row < 0) {
            continue;
        }
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (!item || !shouldManageThumbnail(*item) || !item->hasThumbnail() || item->sourceFilePath().isEmpty()) {
            continue;
        }
        ClipboardItem updated = *item;
        updated.setThumbnail(QPixmap());
        boardModel_->updateItem(row, updated);
    }

    for (const QString &name : desiredNames) {
        const int row = boardModel_->rowForName(name);
        if (row < 0) {
            continue;
        }
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (item && shouldManageThumbnail(*item) && !item->hasThumbnail()) {
            requestThumbnailForItem(*item);
        }
    }

    managedThumbnailNames_ = desiredNames;
}

void ScrollItemsWidget::setVisibleLoadingThumbnailNames(const QSet<QString> &names) {
    QSet<QString> changedNames = visibleLoadingThumbnailNames_;
    changedNames.unite(names);
    visibleLoadingThumbnailNames_ = names;
    updateThumbnailViewport(changedNames);

    if (!thumbnailPulseTimer_) {
        return;
    }

    if (visibleLoadingThumbnailNames_.isEmpty()) {
        thumbnailPulseTimer_->stop();
        return;
    }

    if (!thumbnailPulseTimer_->isActive()) {
        thumbnailPulseTimer_->start();
    }
}

void ScrollItemsWidget::updateThumbnailViewport(const QSet<QString> &names) {
    if (!listView_ || !boardModel_ || !proxyModel_ || names.isEmpty()) {
        return;
    }

    QWidget *viewport = listView_->viewport();
    if (!viewport) {
        return;
    }

    const QRect viewportRect = viewport->rect();
    for (const QString &name : names) {
        const int sourceRow = boardModel_->rowForName(name);
        if (sourceRow < 0) {
            continue;
        }

        const QModelIndex proxyIndex = proxyIndexForSourceRow(sourceRow);
        if (!proxyIndex.isValid()) {
            continue;
        }

        const QRect rect = listView_->visualRect(proxyIndex);
        if (!rect.isValid() || !viewportRect.intersects(rect)) {
            continue;
        }
        viewport->update(rect.adjusted(-2, -2, 2, 2));
    }
}

void ScrollItemsWidget::preRenderAndCleanup() {
    if (!boardModel_ || !cardDelegate_ || !listView_ || !proxyModel_) {
        return;
    }

    // Pre-render all cards into cardPixmapCache_.
    QStyleOptionViewItem baseOption;
    baseOption.initFrom(listView_);
    const qreal dpr = listView_->devicePixelRatioF();
    baseOption.decorationSize = QSize(qRound(dpr), qRound(dpr));
    cardDelegate_->preRenderAll(proxyModel_, baseOption);

    // Release all per-item pixmaps and intermediate caches.
    const int rowCount = boardModel_->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (!item) {
            continue;
        }
        bool needsUpdate = false;
        ClipboardItem updated = *item;
        if (item->hasThumbnail()) {
            updated.setThumbnail(QPixmap());
            needsUpdate = true;
        }
        if (!item->getIcon().isNull()) {
            updated.setIcon(QPixmap());
            needsUpdate = true;
        }
        if (!item->getFavicon().isNull()) {
            updated.setFavicon(QPixmap());
            needsUpdate = true;
        }
        if (needsUpdate) {
            boardModel_->updateItem(row, updated);
        }
    }
    managedThumbnailNames_.clear();
    cardDelegate_->clearIntermediateCaches();
    setVisibleLoadingThumbnailNames({});
}

void ScrollItemsWidget::updateVisibleThumbnails() {
    // No-op: cardPixmapCache_ holds all rendered cards.
    // Intermediate data cleanup happens in preRenderAndCleanup().

}

void ScrollItemsWidget::updateContentWidthHint() {
    if (!isBoardUiVisible()) {
        return;
    }
    if (listView_) {
        if (auto *boardView = dynamic_cast<ClipboardBoardView *>(listView_)) {
            boardView->refreshItemGeometries();
        }
        listView_->viewport()->update();
    }
}

void ScrollItemsWidget::updateEdgeFadeOverlays() {
    QWidget *viewport = listView_ ? listView_->viewport() : nullptr;
    QWidget *overlayHost = ui ? ui->viewHost : nullptr;
    if (!viewport || !overlayHost || !leftEdgeFadeOverlay_ || !rightEdgeFadeOverlay_) {
        return;
    }

    const int fadeWidth = qMin(edgeFadeWidth_, qMax(0, viewport->width() / 3));
    if (fadeWidth <= 0 || viewport->height() <= 0) {
        leftEdgeFadeOverlay_->hide();
        rightEdgeFadeOverlay_->hide();
        return;
    }

    const QPoint viewportTopLeft = viewport->mapTo(overlayHost, QPoint(0, 0));
    const int hostWidth = overlayHost->width();
    const int leftInset = qMax(0, viewportTopLeft.x());
    const int rightInset = qMax(0, hostWidth - (viewportTopLeft.x() + viewport->width()));
    const int leftWidth = qMin(hostWidth, fadeWidth + leftInset);
    const int rightWidth = qMin(hostWidth, fadeWidth + rightInset);
    const int overlayHeight = viewport->height();

    if (leftEdgeFadeOverlay_) {
        auto *overlay = static_cast<EdgeFadeOverlay *>(leftEdgeFadeOverlay_);
        overlay->setTransparentInset(leftInset);
        leftEdgeFadeOverlay_->setGeometry(0,
                                          viewportTopLeft.y(),
                                          leftWidth,
                                          overlayHeight);
    }
    if (rightEdgeFadeOverlay_) {
        auto *overlay = static_cast<EdgeFadeOverlay *>(rightEdgeFadeOverlay_);
        overlay->setTransparentInset(rightInset);
        rightEdgeFadeOverlay_->setGeometry(hostWidth - rightWidth,
                                           viewportTopLeft.y(),
                                           rightWidth,
                                           overlayHeight);
    }
    leftEdgeFadeOverlay_->raise();
    rightEdgeFadeOverlay_->raise();
    leftEdgeFadeOverlay_->show();
    rightEdgeFadeOverlay_->show();
}

void ScrollItemsWidget::startAsyncKeywordSearch() {
    if (currentKeyword_.isEmpty() || !boardService_) {
        return;
    }

    QList<QPair<QString, quint64>> candidates;
    const QList<ClipboardItem> items = boardModel_->items();
    for (const ClipboardItem &item : items) {
        if (item.isMimeDataLoaded() || item.sourceFilePath().isEmpty() || item.contains(currentKeyword_)) {
            continue;
        }

        const ClipboardItem::ContentType type = item.getContentType();
        if (type != ClipboardItem::Text && type != ClipboardItem::RichText) {
            continue;
        }

        candidates.append(qMakePair(item.sourceFilePath(), item.mimeDataFileOffset()));
    }

    if (candidates.isEmpty()) {
        return;
    }

    const quint64 token = keywordSearchToken_;
    boardService_->startAsyncKeywordSearch(candidates, currentKeyword_, token);
}

void ScrollItemsWidget::appendModelItem(const ClipboardItem &item) {
    if (!boardModel_ || item.getName().isEmpty()) {
        return;
    }

    const bool favorite = category == MPasteSettings::STAR_CATEGORY_NAME
        || favoriteFingerprints_.contains(item.fingerprint());
    if (item.isPinned()) {
        const int insertRow = pinnedInsertRow();
        boardModel_->insertItem(insertRow, item, favorite);
    } else {
        boardModel_->appendItem(item, favorite);
    }
    syncPreviewStateForName(item.getName());
}

bool ScrollItemsWidget::appendLoadedItem(const QString &filePath, const ClipboardItem &item) {
    Q_UNUSED(filePath);

    if (!boardModel_ || item.getName().isEmpty()) {
        return false;
    }

    if (boardModel_->rowForName(item.getName()) >= 0) {
        return false;
    }

    appendModelItem(item);
    if (!item.hasThumbnail() && shouldManageThumbnail(item)) {
        if (boardService_) {
            boardService_->processPendingItemAsync(item, item.getName());
        }
    }
    return true;
}

QPair<int, int> ScrollItemsWidget::displaySequenceForIndex(const QModelIndex &proxyIndex) const {
    if (!proxyIndex.isValid() || !proxyModel_) {
        return qMakePair(-1, itemCountForDisplay());
    }

    if (shouldEvictPages()) {
        const int sourceRow = sourceRowForProxyIndex(proxyIndex);
        return qMakePair(sourceRow >= 0 ? (pageBaseOffset_ + sourceRow + 1) : -1,
                         itemCountForDisplay());
    }

    const QModelIndex filterIndex = proxyModel_->mapToSource(proxyIndex);
    return qMakePair(filterIndex.isValid() ? (filterIndex.row() + 1) : -1,
                     itemCountForDisplay());
}

int ScrollItemsWidget::selectedItemCount() const {
    return selectedSourceRows().size();
}

bool ScrollItemsWidget::hasMultipleSelectedItems() const {
    return selectedItemCount() > 1;
}

int ScrollItemsWidget::selectedSourceRow() const {
    const QModelIndex proxyIndex = currentProxyIndex();
    if (proxyIndex.isValid() && proxyModel_) {
        return sourceRowForProxyIndex(proxyIndex);
    }

    const QList<int> selectedRows = selectedSourceRows();
    if (!selectedRows.isEmpty()) {
        return selectedRows.first();
    }
    return -1;
}

const ClipboardItem *ScrollItemsWidget::cacheSelectedItem(int sourceRow) const {
    if (!boardModel_ || sourceRow < 0) {
        return nullptr;
    }

    selectedItemCache_ = boardModel_->itemAt(sourceRow);
    return selectedItemCache_.getName().isEmpty() ? nullptr : &selectedItemCache_;
}

bool ScrollItemsWidget::isBoardUiVisible() const {
    return listView_
        && isVisible()
        && window()
        && window()->isVisible()
        && listView_->viewport()
        && listView_->viewport()->isVisible();
}

bool ScrollItemsWidget::addOneItem(const ClipboardItem &nItem) {
    int row = boardModel_->rowForMatchingItem(nItem);
    if (row < 0) {
        row = boardModel_->rowForFingerprint(nItem.fingerprint());
    }

    if (row >= 0) {
        const ClipboardItem existingItem = boardModel_->itemAt(row);

        // Detect whether the incoming copy carries better data than what
        // is already stored (e.g. the existing item was saved with a bug
        // that dropped MIME content, or the new copy has richer rich-text).
        const bool richerIncomingRichText = nItem.getContentType() == ClipboardItem::RichText
            && existingItem.getContentType() == ClipboardItem::RichText
            && nItem.getNormalizedText().size() > existingItem.getNormalizedText().size();
        const bool contentTypeUpgrade = nItem.getContentType() != existingItem.getContentType()
            && existingItem.getContentType() == ClipboardItem::Text
            && nItem.getContentType() != ClipboardItem::Text;
        // Detect corrupted items: correct type but missing actual content
        // (e.g. Image item saved without image data due to earlier bug).
        const bool existingLacksContent = !existingItem.hasThumbnail()
            && existingItem.getNormalizedText().trimmed().isEmpty()
            && existingItem.getContentType() != ClipboardItem::Color;

        if (richerIncomingRichText || contentTypeUpgrade || existingLacksContent) {
            ClipboardItem updated = nItem;
            updated.setPinned(existingItem.isPinned());
            boardModel_->updateItem(row, updated);
            syncPreviewStateForRow(row);
            if (boardService_) {
                boardService_->processPendingItemAsync(updated, updated.getName());
            }
        }
        const bool alreadyFirst = (row == 0) && (currentPage_ == 0);
        if (!alreadyFirst) {
            moveItemToFirst(row);
        }
        return false;
    }

    const bool favorite = category == MPasteSettings::STAR_CATEGORY_NAME
        || favoriteFingerprints_.contains(nItem.fingerprint());
    const int insertRow = pinnedInsertRow();
    boardModel_->insertItem(insertRow, nItem, favorite);

    // Keep the model within PAGE_SIZE when pagination is active.
    if (paginationEnabled() && boardModel_->rowCount() > PAGE_SIZE) {
        boardModel_->removeItemAt(boardModel_->rowCount() - 1);
    }

    const QModelIndex firstProxyIndex = proxyIndexForSourceRow(insertRow);
    setCurrentProxyIndex(firstProxyIndex);
    ensureLinkPreviewForIndex(firstProxyIndex);
    scheduleThumbnailUpdate();

    if (boardService_) {
        if (!shouldEvictPages()) {
            boardService_->notifyItemAdded();
        }
    }
    trimExpiredItems();
    refreshContentWidthHint();
    emit itemCountChanged(itemCountForDisplay());
    return true;
}

bool ScrollItemsWidget::addAndSaveItem(const ClipboardItem &nItem) {
    // Fast duplicate check before any expensive preparation.
    if (boardModel_) {
        int existingRow = boardModel_->rowForMatchingItem(nItem);
        if (existingRow < 0) {
            existingRow = boardModel_->rowForFingerprint(nItem.fingerprint());
        }
        if (existingRow >= 0) {
            // Delegate to addOneItem which handles update + moveToFirst.
            addOneItem(nItem);
            return false;
        }
    }

    // Also check the on-disk index — during startup the model may not
    // be fully loaded yet, so the model check above can miss duplicates.
    if (boardService_ && boardService_->containsFingerprint(nItem.fingerprint())) {
        return false;
    }

    // Add item to model immediately so the UI can display it right away.
    const bool added = addOneItem(nItem);
    ensureLinkPreviewForIndex(proxyIndexForSourceRow(0));
    if (added && boardService_) {
        // Save + thumbnail generation happen asynchronously to avoid
        // blocking the UI with large items (e.g. Word documents).
        boardService_->processPendingItemAsync(nItem, nItem.getName());

        // Keep loaded-page bookkeeping in sync.
        if (paginationEnabled() && loadedPageTotalItems_ >= 0) {
            loadedPageTotalItems_ = totalItemCountForPagination();
        }
    }
    return added;
}

void ScrollItemsWidget::mergeDeferredMimeFormats(const QString &itemName, const QMap<QString, QByteArray> &extraFormats) {
    if (!boardModel_ || itemName.isEmpty() || extraFormats.isEmpty()) {
        return;
    }

    const int row = boardModel_->rowForName(itemName);
    if (row < 0) {
        // Item not in current model page — just save the extra formats to
        // the item on disk via the board service so they are available when
        // the user pastes.
        if (boardService_) {
            const QString filePath = boardService_->filePathForName(itemName);
            ClipboardItem item = boardService_->loadItemLight(filePath, false);
            if (!item.getName().isEmpty()) {
                item.ensureMimeDataLoaded();
                for (auto it = extraFormats.cbegin(); it != extraFormats.cend(); ++it) {
                    item.setMimeFormat(it.key(), it.value());
                }
                boardService_->saveItemQuiet(item);
            }
        }
        return;
    }

    ClipboardItem item = boardModel_->itemAt(row);
    for (auto it = extraFormats.cbegin(); it != extraFormats.cend(); ++it) {
        item.setMimeFormat(it.key(), it.value());
    }
    boardModel_->updateItem(row, item);

    if (boardService_) {
        boardService_->saveItemQuiet(item);
    }
}

void ScrollItemsWidget::filterByKeyword(const QString &keyword) {
    currentKeyword_ = keyword;
    ++keywordSearchToken_;
    asyncKeywordMatchedNames_.clear();
    applyFilters();
    startAsyncKeywordSearch();
}

void ScrollItemsWidget::filterByType(ClipboardItem::ContentType type) {
    currentTypeFilter_ = type;
    applyFilters();
}

QList<QModelIndex> ScrollItemsWidget::shortcutVisibleIndexes() const {
    QList<QModelIndex> result;
    if (!listView_ || !proxyModel_ || !listView_->viewport()) {
        return result;
    }

    const QRect viewportRect = listView_->viewport()->rect();
    if (!viewportRect.isValid()) {
        return result;
    }

    const int rowCount = proxyModel_->rowCount();
    if (rowCount <= 0) {
        return result;
    }

    const int midY = viewportRect.center().y();
    const QPoint probe(qMax(0, viewportRect.left() + 1), midY);
    QModelIndex startIndex = listView_->indexAt(probe);
    int startRow = startIndex.isValid() ? startIndex.row() : 0;
    if (startRow < 0 || startRow >= rowCount) {
        startRow = 0;
    }

    int firstFullRow = -1;
    for (int row = startRow; row < rowCount; ++row) {
        const QModelIndex idx = proxyModel_->index(row, 0);
        const QRect rect = listView_->visualRect(idx);
        if (!rect.isValid()) {
            continue;
        }
        if (rect.left() > viewportRect.right()) {
            break;
        }
        if (viewportRect.contains(rect)) {
            firstFullRow = row;
            break;
        }
    }

    if (firstFullRow < 0) {
        for (int row = startRow; row < rowCount && result.size() < 10; ++row) {
            result.append(proxyModel_->index(row, 0));
        }
        return result;
    }

    for (int row = firstFullRow; row < rowCount && result.size() < 10; ++row) {
        const QModelIndex idx = proxyModel_->index(row, 0);
        const QRect rect = listView_->visualRect(idx);
        if (!rect.isValid()) {
            continue;
        }
        if (rect.left() > viewportRect.right()) {
            break;
        }
        if (!viewportRect.contains(rect)) {
            continue;
        }
        result.append(idx);
    }

    return result;
}

int ScrollItemsWidget::pinnedInsertRow() const {
    if (!boardModel_) {
        return 0;
    }
    const int rowCount = boardModel_->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (!item || !item->isPinned()) {
            return row;
        }
    }
    return rowCount;
}

int ScrollItemsWidget::unpinnedInsertRowForItem(const ClipboardItem &item, int excludeRow) const {
    if (!boardModel_) {
        return 0;
    }
    const QDateTime targetTime = item.getTime();
    const int rowCount = boardModel_->rowCount();
    int pinnedCount = 0;
    int insertRow = rowCount;

    for (int row = 0; row < rowCount; ++row) {
        if (row == excludeRow) {
            continue;
        }
        const ClipboardItem *candidate = boardModel_->itemPtrAt(row);
        if (!candidate) {
            continue;
        }
        if (candidate->isPinned()) {
            ++pinnedCount;
            continue;
        }
        if (!targetTime.isValid() || !candidate->getTime().isValid()) {
            continue;
        }
        if (targetTime >= candidate->getTime()) {
            insertRow = row;
            break;
        }
    }

    if (insertRow < pinnedCount) {
        insertRow = pinnedCount;
    }
    return insertRow;
}

void ScrollItemsWidget::setShortcutInfo() {
    cleanShortCutInfo();
    if (!proxyModel_ || !boardModel_) {
        return;
    }

    const QList<QModelIndex> indexes = shortcutVisibleIndexes();
    for (int i = 0; i < indexes.size(); ++i) {
        const QModelIndex proxyIndex = indexes.at(i);
        if (!proxyIndex.isValid()) {
            continue;
        }
        const int sourceRow = sourceRowForProxyIndex(proxyIndex);
        if (sourceRow < 0) {
            continue;
        }
        boardModel_->setShortcutText(sourceRow, QStringLiteral("Alt+%1").arg((i + 1) % 10));
    }
}

void ScrollItemsWidget::cleanShortCutInfo() {
    if (boardModel_) {
        boardModel_->clearShortcutTexts();
    }
}

void ScrollItemsWidget::loadFromSaveDir() {
    if (boardService_) {
        boardService_->stopDeferredLoad();
    }
    prepareLoadFromSaveDir();
    if (boardService_) {
        const int initialBatchSize = paginationEnabled() ? PAGED_INITIAL_LOAD_BATCH_SIZE
                                                         : CONTINUOUS_INITIAL_LOAD_BATCH_SIZE;
        boardService_->startAsyncLoad(initialBatchSize, 0);
    }
}

void ScrollItemsWidget::applyScale(int scale) {
    if (!listView_ || !ui) {
        return;
    }

    hideHoverActionBar(false);
    if (hoverActionBar_) {
        hoverActionBar_->deleteLater();
        hoverActionBar_ = nullptr;
    }
    if (hoverHideTimer_) {
        hoverHideTimer_->stop();
        hoverHideTimer_->deleteLater();
        hoverHideTimer_ = nullptr;
    }
    hoverDetailsBtn_ = nullptr;
    hoverAliasBtn_ = nullptr;
    hoverPinBtn_ = nullptr;
    hoverFavoriteBtn_ = nullptr;
    hoverDeleteBtn_ = nullptr;
    hoverOpacity_ = nullptr;

    const QSize itemOuterSize = cardOuterSizeForScale(scale);
    const int spacing = qMax(6, 8 * scale / 100);
    listView_->setSpacing(spacing);
    listView_->setGridSize(QSize(itemOuterSize.width() + spacing, itemOuterSize.height()));

    const int scrollbarHeight = qMax(12, listView_->horizontalScrollBar()->sizeHint().height());
    const int scrollHeight = itemOuterSize.height() + scrollbarHeight;
    edgeContentPadding_ = qMax(6, 8 * scale / 100);
    edgeFadeWidth_ = qMax(12, 16 * scale / 100);
    if (auto *boardView = dynamic_cast<ClipboardBoardView *>(listView_)) {
        boardView->applyViewportMargins(edgeContentPadding_, 0, edgeContentPadding_, 0);
    }
    listView_->setFixedHeight(scrollHeight);
    ui->viewHost->setFixedHeight(scrollHeight);
    setFixedHeight(scrollHeight);

    createHoverActionBar();
    applyTheme(darkTheme_);
    refreshContentWidthHint();
}

void ScrollItemsWidget::loadFromSaveDirDeferred() {
    if (boardService_) {
        boardService_->stopDeferredLoad();
    }
    prepareLoadFromSaveDir();

    if (!boardService_) {
        emit itemCountChanged(itemCountForDisplay());
        return;
    }

    if (paginationEnabled()) {
        boardService_->startAsyncLoad(PAGED_INITIAL_LOAD_BATCH_SIZE, 0);
    } else {
        boardService_->startAsyncLoad(CONTINUOUS_INITIAL_LOAD_BATCH_SIZE,
                                      CONTINUOUS_DEFERRED_LOAD_BATCH_SIZE);
    }
}

QScrollBar *ScrollItemsWidget::horizontalScrollbar() const {
    return listView_ ? listView_->horizontalScrollBar() : nullptr;
}

void ScrollItemsWidget::setAllItemVisible() {
    currentKeyword_.clear();
    currentTypeFilter_ = ClipboardItem::All;
    asyncKeywordMatchedNames_.clear();
    applyFilters();
}

const ClipboardItem *ScrollItemsWidget::currentSelectedItem() const {
    return cacheSelectedItem(selectedSourceRow());
}

const ClipboardItem *ScrollItemsWidget::selectedByShortcut(int keyIndex) {
    if (!proxyModel_ || keyIndex < 0) {
        return currentSelectedItem();
    }

    const QList<QModelIndex> indexes = shortcutVisibleIndexes();
    if (keyIndex >= indexes.size()) {
        return currentSelectedItem();
    }

    const QModelIndex proxyIndex = indexes.at(keyIndex);
    if (!proxyIndex.isValid()) {
        return currentSelectedItem();
    }
    setCurrentProxyIndex(proxyIndex);
    return cacheSelectedItem(sourceRowForProxyIndex(proxyIndex));
}

const ClipboardItem *ScrollItemsWidget::selectedByEnter() {
    return currentSelectedItem();
}

void ScrollItemsWidget::hideHoverTools() {
    hoverProxyIndex_ = QPersistentModelIndex();
    hideHoverActionBar(false);
}

void ScrollItemsWidget::focusMoveLeft() {
    if (!proxyModel_ || proxyModel_->rowCount() <= 0) {
        return;
    }

    QModelIndex current = currentProxyIndex();
    if (!current.isValid()) {
        setCurrentProxyIndex(proxyModel_->index(0, 0));
        return;
    }

    const QModelIndex nextIndex = proxyModel_->index(qMax(0, current.row() - 1), 0);
    setCurrentProxyIndex(nextIndex);
    listView_->scrollTo(nextIndex, QAbstractItemView::EnsureVisible);
}

void ScrollItemsWidget::focusMoveRight() {
    if (!proxyModel_ || proxyModel_->rowCount() <= 0) {
        return;
    }

    QModelIndex current = currentProxyIndex();
    if (!current.isValid()) {
        setCurrentProxyIndex(proxyModel_->index(0, 0));
        return;
    }

    const QModelIndex nextIndex = proxyModel_->index(qMin(proxyModel_->rowCount() - 1, current.row() + 1), 0);
    setCurrentProxyIndex(nextIndex);
    listView_->scrollTo(nextIndex, QAbstractItemView::EnsureVisible);
}

int ScrollItemsWidget::getItemCount() {
    return itemCountForDisplay();
}

void ScrollItemsWidget::setCurrentPageNumber(int pageNumber) {
    if (!paginationEnabled()) {
        return;
    }

    const int totalPages = totalPageCount();
    const int requestedPage = qMax(1, pageNumber);
    const int clampedPage = totalPages > 0 ? qBound(1, requestedPage, totalPages) : 1;
    const int newPageIndex = qMax(0, clampedPage - 1);
    if (currentPage_ == newPageIndex && proxyModel_ && proxyModel_->pageIndex() == newPageIndex) {
        return;
    }

    currentPage_ = newPageIndex;
    syncPageWindow(true);
    if (isBoardUiVisible()) {
        refreshContentWidthHint();
        primeVisibleThumbnailsSync();
        scheduleThumbnailUpdate();
    }
}

int ScrollItemsWidget::currentPageNumber() const {
    if (!paginationEnabled()) {
        return 0;
    }
    return totalPageCount() > 0 ? (currentPage_ + 1) : 0;
}

int ScrollItemsWidget::totalPageCount() const {
    if (!paginationEnabled()) {
        return 0;
    }
    const int totalItems = totalItemCountForPagination();
    return totalItems > 0 ? ((totalItems + PAGE_SIZE - 1) / PAGE_SIZE) : 0;
}

void ScrollItemsWidget::refreshThumbnailCache() {
    updateVisibleThumbnails();
}

int ScrollItemsWidget::maintainPreviewCache(ClipboardBoardService::PreviewCacheMaintenanceMode mode) {
    if (!boardService_) {
        return 0;
    }
    return boardService_->maintainPreviewCache(mode);
}

QSet<QByteArray> ScrollItemsWidget::loadAllFingerprints() {
    if (!boardService_) {
        return {};
    }
    return boardService_->loadAllFingerprints();
}

void ScrollItemsWidget::setFavoriteFingerprints(const QSet<QByteArray> &fingerprints) {
    favoriteFingerprints_ = fingerprints;
    if (!boardModel_) {
        return;
    }

    const int rowCount = boardModel_->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (!item) {
            continue;
        }
        const bool favorite = favoriteFingerprints_.contains(item->fingerprint());
        if (boardModel_->isFavorite(row) != favorite) {
            boardModel_->setFavoriteByFingerprint(item->fingerprint(), favorite);
        }
    }
}

void ScrollItemsWidget::moveSelectedToFirst() {
    const int sourceRow = selectedSourceRow();
    const bool alreadyFirst = (sourceRow == 0) && (currentPage_ == 0);
    if (sourceRow >= 0 && !alreadyFirst) {
        moveItemToFirst(sourceRow);
    }
}

void ScrollItemsWidget::scrollToFirst() {
    if (!proxyModel_) {
        return;
    }

    if (paginationEnabled() && currentPage_ != 0) {
        setCurrentPageNumber(1);
    }

    if (proxyModel_->rowCount() <= 0) {
        return;
    }

    setCurrentProxyIndex(proxyModel_->index(0, 0));
    animateScrollTo(0);
}

void ScrollItemsWidget::scrollToLast() {
    if (!proxyModel_ || proxyModel_->rowCount() <= 0) {
        return;
    }

    setCurrentProxyIndex(proxyModel_->index(proxyModel_->rowCount() - 1, 0));
    animateScrollTo(horizontalScrollbar() ? horizontalScrollbar()->maximum() : 0);
}

QString ScrollItemsWidget::getCategory() const {
    return category;
}

void ScrollItemsWidget::removeItems(const QList<ClipboardItem> &items) {
    if (!boardModel_ || items.isEmpty()) {
        return;
    }

    for (const ClipboardItem &item : items) {
        if (item.getName().isEmpty()) {
            continue;
        }
        pendingThumbnailNames_.remove(item.getName());
        missingThumbnailNames_.remove(item.getName());
        desiredThumbnailNames_.remove(item.getName());
    }

    const int removedCount = ClipboardBoardActionService::removeItems(boardService_, boardModel_, items);
    if (removedCount <= 0) {
        return;
    }

    syncPageWindow(true);
    refreshContentWidthHint();
    emit itemCountChanged(itemCountForDisplay());
}

void ScrollItemsWidget::applyFavoriteToItems(const QList<ClipboardItem> &items, bool favorite) {
    if (!boardModel_ || items.isEmpty()) {
        return;
    }

    for (const ClipboardItem &item : items) {
        const QList<int> rows = ClipboardBoardActionService::resolveRowsForItems(boardModel_, {item});
        const int row = rows.isEmpty() ? -1 : rows.first();
        if (row < 0) {
            continue;
        }

        const ClipboardItem currentItem = boardModel_->itemAt(row);
        const bool isFavorite = boardModel_->isFavorite(row);
        if (isFavorite == favorite) {
            continue;
        }

        setItemFavorite(currentItem, favorite);
        if (favorite) {
            emit itemStared(currentItem);
        } else {
            emit itemUnstared(currentItem);
        }
    }
}

void ScrollItemsWidget::removeItemByContent(const ClipboardItem &item) {
    if (!item.getName().isEmpty()) {
        pendingThumbnailNames_.remove(item.getName());
        missingThumbnailNames_.remove(item.getName());
        desiredThumbnailNames_.remove(item.getName());
    }

    // Remember the position so we can select the next item after removal.
    const int rowBefore = boardModel_ ? boardModel_->rowForName(item.getName()) : -1;

    if (ClipboardBoardActionService::removeItem(boardService_, boardModel_, item)) {
        // Select the item that now occupies the deleted position (i.e. the
        // next item), or the last item if we deleted the tail.
        if (boardModel_ && rowBefore >= 0) {
            const int nextRow = qMin(rowBefore, boardModel_->rowCount() - 1);
            if (nextRow >= 0) {
                setCurrentProxyIndex(proxyIndexForSourceRow(nextRow));
            }
        }

        if (paginationEnabled() && loadedPageTotalItems_ > 0) {
            loadedPageTotalItems_ = totalItemCountForPagination();
        }
        refreshContentWidthHint();
        emit itemCountChanged(itemCountForDisplay());
        emit pageStateChanged(currentPageNumber(), totalPageCount());
    }
}

void ScrollItemsWidget::setItemFavorite(const ClipboardItem &item, bool favorite) {
    if (favorite) {
        favoriteFingerprints_.insert(item.fingerprint());
    } else {
        favoriteFingerprints_.remove(item.fingerprint());
    }
    ClipboardBoardActionService::setFavorite(boardModel_, item, favorite);
}

void ScrollItemsWidget::syncAlias(const QByteArray &fingerprint, const QString &alias) {
    if (!boardModel_ || fingerprint.isEmpty()) {
        return;
    }

    const int rowCount = boardModel_->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        const ClipboardItem *item = boardModel_->itemPtrAt(row);
        if (!item || item->fingerprint() != fingerprint) {
            continue;
        }
        ClipboardItem updated = boardModel_->itemAt(row);
        if (updated.getAlias() == alias) {
            continue;
        }
        updated.setAlias(alias);
        boardModel_->updateItem(row, updated);
        if (boardService_) {
            ClipboardBoardActionService::persistItemMetadata(boardService_, boardModel_, row, updated);
        }
    }
}

QList<ClipboardItem> ScrollItemsWidget::allItems() {
    ensureAllItemsLoaded();
    return boardModel_->items();
}

bool ScrollItemsWidget::handleWheelScroll(QWheelEvent *event) {
    if (!event) {
        return false;
    }

    QScrollBar *scrollBar = horizontalScrollbar();
    if (!scrollBar || (scrollBar->maximum() <= 0 && (!boardService_ || !boardService_->hasPendingItems()))) {
        return false;
    }

    int delta = 0;
    const QPoint pixelDelta = event->pixelDelta();
    if (!pixelDelta.isNull()) {
        if (qAbs(pixelDelta.x()) < qAbs(pixelDelta.y()) || pixelDelta.x() == 0) {
            return false;
        }
        delta = qRound((-pixelDelta.x()) * 1.6);
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

void ScrollItemsWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refreshContentWidthHint();
    updateEdgeFadeOverlays();
    if (boardService_) {
        boardService_->setVisibleHint(isVisible() && window() && window()->isVisible());
    }
    setFirstVisibleItemSelected();
    scheduleThumbnailUpdate();
    updateLoadingOverlay();
}

void ScrollItemsWidget::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    hideHoverActionBar(false);
    if (boardService_) {
        boardService_->setVisibleHint(false);
    }
}

void ScrollItemsWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    refreshContentWidthHint();
    scheduleThumbnailUpdate();
    updateLoadingOverlay();
}

bool ScrollItemsWidget::eventFilter(QObject *watched, QEvent *event) {
    if (!listView_) {
        return QWidget::eventFilter(watched, event);
    }

    const bool watchingHoverBar = hoverActionBar_
        && (watched == hoverActionBar_
            || watched == hoverDetailsBtn_
            || watched == hoverAliasBtn_
            || watched == hoverPinBtn_
            || watched == hoverFavoriteBtn_
            || watched == hoverDeleteBtn_);

    if (watchingHoverBar) {
        if (event->type() == QEvent::Enter
            || event->type() == QEvent::HoverEnter
            || event->type() == QEvent::MouseMove) {
            if (hoverHideTimer_ && hoverHideTimer_->isActive()) {
                hoverHideTimer_->stop();
            }
            return QWidget::eventFilter(watched, event);
        }

        if (event->type() == QEvent::Leave || event->type() == QEvent::HoverLeave) {
            if (hoverActionBar_ && hoverHideTimer_) {
                const QPoint hoverPoint = hoverActionBar_->mapFromGlobal(QCursor::pos());
                if (!hoverActionBar_->rect().contains(hoverPoint)) {
                    hoverHideTimer_->start();
                }
            }
            return QWidget::eventFilter(watched, event);
        }
    }

    if (event->type() == QEvent::Wheel && (watched == listView_ || watched == listView_->viewport())) {
        const bool handled = handleWheelScroll(static_cast<QWheelEvent *>(event));
        if (handled) {
            updateHoverActionBarPosition();
            scheduleThumbnailUpdate();
        }
        return handled;
    }

    if (event->type() == QEvent::Resize && watched == listView_->viewport()) {
        refreshContentWidthHint();
        updateHoverActionBarPosition();
        scheduleThumbnailUpdate();
        updateLoadingOverlay();
    }

    if ((watched == listView_ || watched == listView_->viewport()) && event->type() == QEvent::MouseMove) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (hoverActionBar_ && hoverActionBar_->isVisible()) {
            auto *watchedWidget = qobject_cast<QWidget *>(watched);
            const QPoint hoverPoint = watchedWidget && hoverActionBar_->parentWidget()
                ? watchedWidget->mapTo(hoverActionBar_->parentWidget(), mouseEvent->pos())
                : mouseEvent->pos();
            if (hoverActionBar_->geometry().contains(hoverPoint)) {
                return QWidget::eventFilter(watched, event);
            }
        }
        const QModelIndex hoverIndex = listView_->indexAt(mouseEvent->pos());
        updateHoverActionBar(hoverIndex);
        return QWidget::eventFilter(watched, event);
    }

    if ((watched == listView_ || watched == listView_->viewport())
        && (event->type() == QEvent::Leave || event->type() == QEvent::HoverLeave)) {
        if (hoverHideTimer_ && !hoverHideTimer_->isActive()) {
            hoverHideTimer_->start();
        }
    }

    if (event->type() == QEvent::KeyPress) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Delete && selectedItemCount() > 0) {
            const QList<ClipboardItem> selection = selectedItems();
            if (category == MPasteSettings::STAR_CATEGORY_NAME) {
                applyFavoriteToItems(selection, false);
            } else {
                removeItems(selection);
            }
            return true;
        }
        QGuiApplication::sendEvent(parent(), event);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
