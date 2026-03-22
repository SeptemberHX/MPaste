// input: Depends on ScrollItemsWidget.h, ClipboardBoardService, Qt model/view APIs, and delegate-based card painting.
// output: Implements lazy-loaded boards, proxy filtering, async thumbnail completion, and list-view item interaction.
// pos: Widget-layer board implementation driving clipboard and favorites history lists.
// update: If I change, update this header block and my folder README.md (arrow navigation no longer forces center + hover action bar + main-card save/export + multi-select batch actions + data-layer preview kind + board paint FPS logging + hidden-stage light prewarm).
// note: Added dark theme rendering hooks, metadata-save rehydrate fixes, alias sync, loading overlay, board paint FPS logging, and hidden-stage light prewarm.
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
    proxyModel_ = new ClipboardBoardProxyModel(this);
    proxyModel_->setSourceModel(boardModel_);
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
    connect(boardService_, &ClipboardBoardService::localPersistenceChanged, this, &ScrollItemsWidget::localPersistenceChanged);

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

    thumbnailUpdateTimer_ = new QTimer(this);
    thumbnailUpdateTimer_->setSingleShot(true);
    thumbnailUpdateTimer_->setInterval(80);
    connect(thumbnailUpdateTimer_, &QTimer::timeout, this, &ScrollItemsWidget::updateVisibleThumbnails);

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
            const int sourceRow = proxyModel_->mapToSource(hoverProxyIndex_).row();
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
    if (!proxyModel_ || !boardModel_ || sourceRow < 0) {
        return {};
    }
    return proxyModel_->mapFromSource(boardModel_->index(sourceRow, 0));
}

QList<QModelIndex> ScrollItemsWidget::selectedProxyIndexes() const {
    QList<QModelIndex> result;
    if (!listView_ || !listView_->selectionModel()) {
        return result;
    }

    result = listView_->selectionModel()->selectedRows();
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
        const int sourceRow = proxyModel_->mapToSource(proxyIndex).row();
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
    hoverBar->setCornerRadius(borderRadius);
    hoverBar->setColors(dark ? QColor(26, 31, 38, 205) : QColor(255, 255, 255, 185),
                        dark ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 120));

    hoverOpacity_ = new QGraphicsOpacityEffect(hoverActionBar_);
    hoverOpacity_->setOpacity(0.0);
    hoverActionBar_->setGraphicsEffect(hoverOpacity_);

    auto *layout = new QHBoxLayout(hoverActionBar_);
    layout->setContentsMargins(margin, 0, margin, 0);
    layout->setSpacing(spacing);

    auto createButton = [scale, dark](const QString &iconPath, const QString &tooltip) {
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
        const int sourceRow = proxyModel_->mapToSource(hoverProxyIndex_).row();
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
        const int sourceRow = proxyModel_->mapToSource(hoverProxyIndex_).row();
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
        const int sourceRow = proxyModel_->mapToSource(hoverProxyIndex_).row();
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
        const int sourceRow = proxyModel_->mapToSource(hoverProxyIndex_).row();
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
        const int sourceRow = proxyModel_->mapToSource(hoverProxyIndex_).row();
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

    const int sourceRow = proxyModel_->mapToSource(proxyIndex).row();
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

    const int sourceRow = proxyModel_->mapToSource(proxyIndex).row();
    if (sourceRow < 0) {
        return;
    }

    ClipboardItem item = boardModel_->itemAt(sourceRow);
    if (item.getContentType() != ClipboardItem::Link || item.hasThumbnail()) {
        return;
    }

    QString linkText = item.getUrl().trimmed();
    if (linkText.isEmpty()) {
        linkText = item.getNormalizedText().trimmed();
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

        if (boardModel_->updateItem(row, updated) && boardService_) {
            boardService_->saveItem(updated);
        }
    });
    fetcher->handle();
}

void ScrollItemsWidget::handleActivatedIndex(const QModelIndex &index) {
    setCurrentProxyIndex(index);
    const ClipboardItem *selectedItem = selectedByEnter();
    if (selectedItem) {
        emit doubleClicked(*selectedItem);
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

    const bool dark = darkTheme_;
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
    const int sourceRow = proxyModel_->mapToSource(proxyIndex).row();
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
        boardModel_->updateItem(row, item);
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

    ClipboardItem item = boardModel_->itemAt(row);
    if (!shouldManageThumbnail(item)) {
        return;
    }
    if (!thumbnail.isNull()) {
        missingThumbnailNames_.remove(expectedName);
        item.setThumbnail(thumbnail);
        boardModel_->updateItem(row, item);
    } else {
        missingThumbnailNames_.insert(expectedName);
    }

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
    emit itemCountChanged(itemCountForDisplay());
}

void ScrollItemsWidget::handleDeferredLoadCompleted() {
    if (isBoardUiVisible()) {
        refreshContentWidthHint();
        scheduleThumbnailUpdate();
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

void ScrollItemsWidget::applyFilters() {
    if ((!currentKeyword_.isEmpty() || currentTypeFilter_ != ClipboardItem::All)
        && boardService_ && boardService_->hasPendingItems()) {
        ensureAllItemsLoaded();
    }

    proxyModel_->setTypeFilter(currentTypeFilter_);
    proxyModel_->setKeyword(currentKeyword_);
    proxyModel_->setAsyncMatchedNames(asyncKeywordMatchedNames_);
    if (isBoardUiVisible()) {
        setFirstVisibleItemSelected();
        refreshContentWidthHint();
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

    const int targetRow = item.isPinned() ? 0 : pinnedInsertRow();
    if (targetRow != sourceRow) {
        boardModel_->moveItemToRow(sourceRow, targetRow);
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
    if (!boardService_ || boardService_->deferredLoadActive() || !boardService_->hasPendingItems()
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
    if (currentKeyword_.isEmpty() && currentTypeFilter_ == ClipboardItem::All) {
        return boardService_ ? boardService_->totalItemCount() : 0;
    }
    return proxyModel_ ? proxyModel_->rowCount() : 0;
}

void ScrollItemsWidget::trimExpiredItems() {
    if (category == MPasteSettings::STAR_CATEGORY_NAME || !boardService_) {
        return;
    }

    const QDateTime cutoff = MPasteSettings::getInst()->historyRetentionCutoff();
    boardService_->trimExpiredPendingItems(cutoff);

    while (boardModel_ && boardModel_->rowCount() > 0) {
        const ClipboardItem lastItem = boardModel_->itemAt(boardModel_->rowCount() - 1);
        if (lastItem.getName().isEmpty() || lastItem.getTime() >= cutoff) {
            break;
        }

        boardService_->deleteItemByPath(boardService_->filePathForItem(lastItem));
        boardModel_->removeItemAt(boardModel_->rowCount() - 1);
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
    boardService_->refreshIndex();
    updateContentWidthHint();
    updateLoadingOverlay();
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
    if (!thumbnailUpdateTimer_) {
        return;
    }
    if (!isBoardUiVisible()) {
        setVisibleLoadingThumbnailNames({});
        return;
    }
    thumbnailUpdateTimer_->start();
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

        ClipboardItem item = boardModel_->itemAt(row);
        if (!shouldManageThumbnail(item) || !item.hasThumbnail() || item.sourceFilePath().isEmpty()) {
            continue;
        }

        item.setThumbnail(QPixmap());
        boardModel_->updateItem(row, item);
    }

    for (const QString &name : desiredNames) {
        const int row = boardModel_->rowForName(name);
        if (row < 0) {
            continue;
        }

        const ClipboardItem item = boardModel_->itemAt(row);
        if (shouldManageThumbnail(item) && !item.hasThumbnail()) {
            requestThumbnailForItem(item);
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

void ScrollItemsWidget::updateVisibleThumbnails() {
    if (!boardModel_ || !proxyModel_ || !listView_) {
        return;
    }
    if (!isBoardUiVisible()) {
        setVisibleLoadingThumbnailNames({});
        return;
    }

    const int proxyCount = proxyModel_->rowCount();
    if (proxyCount <= 0) {
        desiredThumbnailNames_.clear();
        applyManagedThumbnailNames({});
        setVisibleLoadingThumbnailNames({});
        return;
    }

    const QRect viewportRect = listView_->viewport()->rect();
    const QPoint leftProbe(viewportRect.left() + 1, viewportRect.center().y());
    const QPoint rightProbe(viewportRect.right() - 1, viewportRect.center().y());
    QModelIndex leftIndex = listView_->indexAt(leftProbe);
    QModelIndex rightIndex = listView_->indexAt(rightProbe);
    const int gridWidth = qMax(1, listView_->gridSize().width());
    const int estimatedVisibleCount = qMax(1, viewportRect.width() / gridWidth + 2);
    int visibleStartRow = 0;
    if (leftIndex.isValid()) {
        visibleStartRow = leftIndex.row();
    } else if (rightIndex.isValid()) {
        visibleStartRow = qMax(0, rightIndex.row() - estimatedVisibleCount + 1);
    } else {
        const QModelIndex currentIndex = currentProxyIndex();
        visibleStartRow = currentIndex.isValid() ? currentIndex.row() : 0;
    }

    int visibleEndRow = 0;
    if (rightIndex.isValid()) {
        visibleEndRow = rightIndex.row();
    } else {
        visibleEndRow = qMin(proxyCount - 1, visibleStartRow + estimatedVisibleCount - 1);
    }
    if (visibleStartRow > visibleEndRow) {
        std::swap(visibleStartRow, visibleEndRow);
    }

    const int prefetch = MPasteSettings::getInst()->getThumbnailPrefetchCount();
    const int startRow = qMax(0, visibleStartRow - prefetch);
    const int endRow = qMin(proxyCount - 1, visibleEndRow + prefetch);

    QSet<QString> desiredNames;
    QSet<QString> visibleLoadingNames;
    for (int proxyRow = startRow; proxyRow <= endRow; ++proxyRow) {
        const QModelIndex proxyIndex = proxyModel_->index(proxyRow, 0);
        if (!proxyIndex.isValid()) {
            continue;
        }
        const int sourceRow = proxyModel_->mapToSource(proxyIndex).row();
        if (sourceRow < 0) {
            continue;
        }
        const ClipboardItem item = boardModel_->itemAt(sourceRow);
        const QString name = item.getName();
        if (name.isEmpty() || !shouldManageThumbnail(item)) {
            continue;
        }

        desiredNames.insert(name);
        if (item.hasThumbnail()) {
            missingThumbnailNames_.remove(name);
        }

        if (proxyRow >= visibleStartRow
            && proxyRow <= visibleEndRow
            && item.getContentType() != ClipboardItem::Link
            && !item.hasThumbnail()) {
            visibleLoadingNames.insert(name);
        }
    }

    desiredThumbnailNames_ = desiredNames;
    applyManagedThumbnailNames(desiredNames);
    setVisibleLoadingThumbnailNames(visibleLoadingNames);
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

bool ScrollItemsWidget::appendLoadedItem(const QString &filePath, const ClipboardItem &item) {
    int existingRow = boardModel_->rowForMatchingItem(item);
    if (existingRow < 0) {
        existingRow = boardModel_->rowForFingerprint(item.fingerprint());
    }
    if (existingRow >= 0) {
        if (boardService_) {
            boardService_->deleteItemByPath(filePath);
        }
        return false;
    }

    const bool favorite = category == MPasteSettings::STAR_CATEGORY_NAME
        || favoriteFingerprints_.contains(item.fingerprint());
    if (item.isPinned()) {
        const int insertRow = pinnedInsertRow();
        boardModel_->insertItem(insertRow, item, favorite);
    } else {
        boardModel_->appendItem(item, favorite);
    }
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
    return qMakePair(proxyIndex.row() + 1, proxyModel_->rowCount());
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
        return proxyModel_->mapToSource(proxyIndex).row();
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
        const QMimeData *incomingMimeData = nItem.isMimeDataLoaded() ? nItem.getMimeData() : nullptr;
        const QMimeData *existingMimeData = existingItem.isMimeDataLoaded() ? existingItem.getMimeData() : nullptr;
        if (incomingMimeData && existingMimeData
            && incomingMimeData->hasHtml() && existingMimeData->hasHtml()
            && incomingMimeData->html().length() > existingMimeData->html().length()) {
            if (boardService_) {
                boardService_->removeItemFile(boardService_->filePathForItem(existingItem));
            }
            ClipboardItem updated = nItem;
            updated.setPinned(existingItem.isPinned());
            boardModel_->updateItem(row, updated);
            if (!nItem.isMimeDataLoaded() && nItem.sourceFilePath().isEmpty()) {
                if (boardService_) {
                    boardService_->processPendingItemAsync(updated, updated.getName());
                }
            } else {
                if (boardService_) {
                    boardService_->saveItem(updated);
                }
            }
        }
        if (row > 0) {
            moveItemToFirst(row);
        }
        return false;
    }

    const bool favorite = category == MPasteSettings::STAR_CATEGORY_NAME
        || favoriteFingerprints_.contains(nItem.fingerprint());
    const int insertRow = pinnedInsertRow();
    boardModel_->insertItem(insertRow, nItem, favorite);
    const QModelIndex firstProxyIndex = proxyIndexForSourceRow(insertRow);
    setCurrentProxyIndex(firstProxyIndex);
    ensureLinkPreviewForIndex(firstProxyIndex);
    scheduleThumbnailUpdate();

    if (boardService_) {
        boardService_->notifyItemAdded();
    }
    trimExpiredItems();
    refreshContentWidthHint();
    emit itemCountChanged(itemCountForDisplay());
    return true;
}

bool ScrollItemsWidget::addAndSaveItem(const ClipboardItem &nItem) {
    const bool isPendingClipboardSnapshot = !nItem.isMimeDataLoaded() && nItem.sourceFilePath().isEmpty();
    ClipboardItem preparedItem = isPendingClipboardSnapshot
        ? nItem
        : (boardService_ ? boardService_->prepareItemForSave(nItem) : nItem);
    const bool added = addOneItem(preparedItem);
    ensureLinkPreviewForIndex(proxyIndexForSourceRow(0));
    if (added) {
        if (isPendingClipboardSnapshot) {
            if (boardService_) {
                boardService_->processPendingItemAsync(preparedItem, preparedItem.getName());
            }
        } else {
            if (boardService_) {
                boardService_->saveItem(preparedItem);
            }
        }
    }
    return added;
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
        const int sourceRow = proxyModel_->mapToSource(proxyIndex).row();
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
        boardService_->startAsyncLoad(INITIAL_LOAD_BATCH_SIZE, 0);
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

    boardService_->startAsyncLoad(qMin(DEFERRED_LOAD_BATCH_SIZE, INITIAL_LOAD_BATCH_SIZE),
                                  DEFERRED_LOAD_BATCH_SIZE);
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
    const int row = moveItemToFirst(proxyModel_->mapToSource(proxyIndex).row());
    return cacheSelectedItem(row);
}

const ClipboardItem *ScrollItemsWidget::selectedByEnter() {
    const int sourceRow = selectedSourceRow();
    if (sourceRow < 0) {
        return nullptr;
    }

    const int row = moveItemToFirst(sourceRow);
    return cacheSelectedItem(row);
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

void ScrollItemsWidget::scrollToFirst() {
    if (!proxyModel_ || proxyModel_->rowCount() <= 0) {
        return;
    }

    setCurrentProxyIndex(proxyModel_->index(0, 0));
    animateScrollTo(0);
}

void ScrollItemsWidget::scrollToLast() {
    ensureAllItemsLoaded();
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

    setFirstVisibleItemSelected();
    maybeLoadMoreItems();
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
    if (ClipboardBoardActionService::removeItem(boardService_, boardModel_, item)) {
        setFirstVisibleItemSelected();
        maybeLoadMoreItems();
        refreshContentWidthHint();
        emit itemCountChanged(itemCountForDisplay());
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
