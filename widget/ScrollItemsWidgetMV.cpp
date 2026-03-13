// input: Depends on ScrollItemsWidget.h, LocalSaver, Qt model/view APIs, and delegate-based card painting.
// output: Implements lazy-loaded boards, proxy filtering, async thumbnail completion, and list-view item interaction.
// pos: Widget-layer board implementation driving clipboard and favorites history lists.
// update: If I change, update this header block and my folder README.md (arrow navigation no longer forces center + hover action bar).
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QBuffer>
#include <QDebug>
#include <QImage>
#include <QImageReader>
#include <QItemSelectionModel>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScreen>
#include <QScroller>
#include <QShowEvent>
#include <QTextDocument>
#include <QToolButton>
#include <QGraphicsOpacityEffect>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <utils/MPasteSettings.h>

#include "ClipboardBoardModel.h"
#include "ClipboardBoardProxyModel.h"
#include "ClipboardCardDelegate.h"
#include "ClipboardItemPreviewDialog.h"
#include "CardMetrics.h"
#include "ScrollItemsWidget.h"
#include "ui_ScrollItemsWidget.h"
#include "utils/OpenGraphFetcher.h"
#include "utils/PlatformRelated.h"

namespace {

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

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>

enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor; // AABBGGRR
    DWORD AnimationId;
};

enum WINDOWCOMPOSITIONATTRIB {
    WCA_ACCENT_POLICY = 19
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

static void enableBlurBehindForWidget(HWND hwnd) {
    if (!hwnd) {
        return;
    }

    auto user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }
    auto setWCA = reinterpret_cast<pfnSetWindowCompositionAttribute>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!setWCA) {
        return;
    }

    DWORD tint = (28u << 24) | (255u << 16) | (255u << 8) | 255u; // rgba(255,255,255,28)

    ACCENT_POLICY accent{};
    accent.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    accent.AccentFlags = 2;
    accent.GradientColor = tint;

    WINDOWCOMPOSITIONATTRIBDATA data{};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);
    setWCA(hwnd, &data);
}
#endif
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

bool isVeryTallImage(const QSize &size) {
    return size.isValid() && size.height() >= qMax(4000, size.width() * 4);
}

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

class ClipboardBoardView final : public QListView {
public:
    explicit ClipboardBoardView(QWidget *parent = nullptr)
        : QListView(parent) {}

    void applyViewportMargins(int left, int top, int right, int bottom) {
        setViewportMargins(left, top, right, bottom);
    }

    void refreshItemGeometries() {
        updateGeometries();
    }
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

    QPixmap scaled = fullImage.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
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

QImage buildCardThumbnailImageFromBytes(const QByteArray &imageBytes, qreal targetDpr) {
    if (imageBytes.isEmpty()) {
        return QImage();
    }

    constexpr int cardW = 275;
    constexpr int cardH = 218;
    const QSize pixelTargetSize = QSize(cardW, cardH) * targetDpr;
    if (!pixelTargetSize.isValid()) {
        return QImage();
    }

    QBuffer buffer;
    buffer.setData(imageBytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return QImage();
    }

    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);

    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        const QSize scaledSize = sourceSize.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding);
        if (scaledSize.isValid()) {
            reader.setScaledSize(scaledSize);
        }
    }

    QImage decoded = reader.read();
    if (decoded.isNull()) {
        decoded.loadFromData(imageBytes);
    }
    if (decoded.isNull()) {
        return QImage();
    }

    if (decoded.size() != pixelTargetSize) {
        decoded = decoded.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    if (decoded.isNull()) {
        return QImage();
    }

    const int x = qMax(0, (decoded.width() - pixelTargetSize.width()) / 2);
    const int y = qMax(0, (decoded.height() - pixelTargetSize.height()) / 2);
    QImage thumbnail = decoded.copy(x, y,
                                    qMin(decoded.width(), pixelTargetSize.width()),
                                    qMin(decoded.height(), pixelTargetSize.height()));
    thumbnail.setDevicePixelRatio(targetDpr);
    return thumbnail;
}

QImage buildRichTextThumbnailImageFromHtml(const QString &html, const QByteArray &imageBytes, qreal thumbnailDpr) {
    if (html.isEmpty()) {
        return QImage();
    }

    constexpr int cardW = 275;
    constexpr int cardH = 218;
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
    const QString imageSource = firstHtmlImageSource(html);
    if (!imageSource.isEmpty() && !imageBytes.isEmpty()) {
        QImage image;
        if (image.loadFromData(imageBytes)) {
            document.addResource(QTextDocument::ImageResource, QUrl(imageSource), image);
        }
    }
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

    snapshot.setDevicePixelRatio(thumbnailDpr);
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

ScrollItemsWidget::ScrollItemsWidget(const QString &category, const QString &borderColor, QWidget *parent)
    : QWidget(parent),
      ui(new Ui::ScrollItemsWidget),
      category(category),
      borderColor(borderColor),
      saver(new LocalSaver()),
      scrollAnimation(nullptr) {
    ui->setupUi(this);

    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize itemOuterSize = cardOuterSizeForScale(scale);

    auto *hostLayout = new QVBoxLayout(ui->viewHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    auto *boardView = new ClipboardBoardView(ui->viewHost);
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
    listView_->setSelectionMode(QAbstractItemView::SingleSelection);
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
    scroller->setScrollerProperties(sp);

    listView_->horizontalScrollBar()->setSingleStep(48);
    scrollAnimation = new QPropertyAnimation(listView_->horizontalScrollBar(), "value", this);
    scrollAnimation->setEasingCurve(QEasingCurve::OutQuad);
    scrollAnimation->setDuration(70);

    deferredLoadTimer_ = new QTimer(this);
    deferredLoadTimer_->setSingleShot(true);
    connect(deferredLoadTimer_, &QTimer::timeout, this, &ScrollItemsWidget::continueDeferredLoad);

    connect(listView_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &ScrollItemsWidget::handleCurrentIndexChanged);
    connect(listView_, &QListView::clicked, this, [this](const QModelIndex &index) {
        setCurrentProxyIndex(index);
    });
    connect(listView_, &QListView::doubleClicked, this, &ScrollItemsWidget::handleActivatedIndex);
    connect(listView_, &QListView::customContextMenuRequested, this, &ScrollItemsWidget::showContextMenu);
    connect(listView_->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) { maybeLoadMoreItems(); });

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
}

ScrollItemsWidget::~ScrollItemsWidget() {
    deferredLoadActive_ = false;
    if (deferredLoadThread_) {
        deferredLoadThread_->wait();
    }
    if (keywordSearchThread_) {
        keywordSearchThread_->wait();
    }
    for (QThread *thread : processingThreads_) {
        if (thread) {
            thread->wait();
        }
    }
    delete ui;
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
}

void ScrollItemsWidget::createHoverActionBar() {
    if (!listView_) {
        return;
    }

    const int scale = MPasteSettings::getInst()->getItemScale();
    const int barHeight = qMax(20, 28 * scale / 100);
    const int borderRadius = qMax(6, 8 * scale / 100);
    const int margin = qMax(4, 6 * scale / 100);
    const int spacing = qMax(2, 4 * scale / 100);

    auto *hoverBar = new HoverActionBar(listView_->viewport());
    hoverActionBar_ = hoverBar;
    hoverActionBar_->setObjectName(QStringLiteral("cardActionBar"));
    hoverActionBar_->setFixedHeight(barHeight);
    hoverActionBar_->setFocusPolicy(Qt::NoFocus);
    hoverActionBar_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    hoverActionBar_->setAttribute(Qt::WA_TranslucentBackground);
    hoverBar->setCornerRadius(borderRadius);
    hoverBar->setColors(QColor(255, 255, 255, 185), QColor(255, 255, 255, 120));

#ifdef Q_OS_WIN
    hoverActionBar_->setAttribute(Qt::WA_NativeWindow);
    enableBlurBehindForWidget(reinterpret_cast<HWND>(hoverActionBar_->winId()));
#endif

    hoverOpacity_ = new QGraphicsOpacityEffect(hoverActionBar_);
    hoverOpacity_->setOpacity(0.0);
    hoverActionBar_->setGraphicsEffect(hoverOpacity_);

    auto *layout = new QHBoxLayout(hoverActionBar_);
    layout->setContentsMargins(margin, 0, margin, 0);
    layout->setSpacing(spacing);

    auto createButton = [scale](const QString &iconPath, const QString &tooltip) {
        const int iconSz = qMax(12, 14 * scale / 100);
        const int btnSz = qMax(18, 22 * scale / 100);
        const int borderR = qMax(4, 6 * scale / 100);
        const int pad = qMax(2, 3 * scale / 100);

        auto *button = new QToolButton;
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(iconSz, iconSz));
        button->setFixedSize(btnSz, btnSz);
        button->setStyleSheet(QString(R"(
            QToolButton {
                background: transparent;
                border: none;
                border-radius: %1px;
                padding: %2px;
            }
            QToolButton:hover {
                background: rgba(0, 0, 0, 0.08);
            }
        )").arg(borderR).arg(pad));
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tooltip);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };

    hoverDetailsBtn_ = createButton(QStringLiteral(":/resources/resources/details.svg"), detailsLabel());
    hoverFavoriteBtn_ = createButton(QStringLiteral(":/resources/resources/star_outline.svg"), tr("Add to favorites"));
    hoverDeleteBtn_ = createButton(QStringLiteral(":/resources/resources/delete.svg"), tr("Delete"));

    layout->addWidget(hoverDetailsBtn_);
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
    hoverFavoriteBtn_->setIcon(QIcon(favorite
        ? QStringLiteral(":/resources/resources/star_filled.svg")
        : QStringLiteral(":/resources/resources/star_outline.svg")));
    hoverFavoriteBtn_->setToolTip(favorite
        ? tr("Remove from favorites")
        : tr("Add to favorites"));
}

void ScrollItemsWidget::updateHoverActionBar(const QModelIndex &proxyIndex) {
    if (!hoverActionBar_ || !listView_ || !proxyModel_ || !boardModel_) {
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

    const QRect itemRect = listView_->visualRect(hoverProxyIndex_);
    if (!itemRect.isValid() || !listView_->viewport()->rect().intersects(itemRect)) {
        hideHoverActionBar();
        return;
    }

    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize cardSize(kCardBaseWidth * scale / 100, kCardBaseHeight * scale / 100);
    const QRect cardRect(itemRect.topLeft(), cardSize);
    hoverActionBar_->adjustSize();
    const int x = cardRect.left() + (cardRect.width() - hoverActionBar_->width()) / 2;
    const int y = cardRect.top() + qMax(2, 4 * scale / 100);
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

        if (boardModel_->updateItem(row, updated)) {
            saveItem(updated);
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

    setCurrentProxyIndex(proxyIndex);
    const int sourceRow = proxyModel_->mapToSource(proxyIndex).row();
    const ClipboardItem item = boardModel_->itemAt(sourceRow);
    if (item.getName().isEmpty()) {
        return;
    }

    const bool isFavorite = boardModel_->isFavorite(sourceRow);
    const QList<QUrl> urls = item.getNormalizedUrls();
    const bool canOpenFolder = item.getContentType() == ClipboardItem::File
        && !urls.isEmpty()
        && std::all_of(urls.begin(), urls.end(), [](const QUrl &url) { return url.isLocalFile(); });

    QMenu menu(this);
    menu.addAction(QIcon(QStringLiteral(":/resources/resources/text_plain.svg")), plainTextPasteLabel(), [this]() {
        const ClipboardItem *selectedItem = selectedByEnter();
        if (selectedItem) {
            emit plainTextPasteRequested(*selectedItem);
        }
    });
    menu.addAction(QIcon(QStringLiteral(":/resources/resources/details.svg")), detailsLabel(), [this, proxyIndex]() {
        setCurrentProxyIndex(proxyIndex);
        const ClipboardItem *selectedItem = currentSelectedItem();
        if (!selectedItem) {
            return;
        }
        const QPair<int, int> sequenceInfo = displaySequenceForIndex(proxyIndex);
        emit detailsRequested(*selectedItem, sequenceInfo.first, sequenceInfo.second);
    });
    if (ClipboardItemPreviewDialog::supportsPreview(item)) {
        menu.addAction(QIcon(QStringLiteral(":/resources/resources/preview.svg")), tr("Preview"), [this]() {
            const ClipboardItem *selectedItem = currentSelectedItem();
            if (selectedItem) {
                emit previewRequested(*selectedItem);
            }
        });
    }
    if (canOpenFolder) {
        menu.addAction(QIcon(QStringLiteral(":/resources/resources/files.svg")), openContainingFolderLabel(), [urls]() {
            PlatformRelated::revealInFileManager(urls);
        });
    }

    menu.addSeparator();
    menu.addAction(QIcon(isFavorite
                         ? QStringLiteral(":/resources/resources/star_filled.svg")
                         : QStringLiteral(":/resources/resources/star.svg")),
                   isFavorite ? tr("Remove from favorites") : tr("Add to favorites"),
                   [this, item, isFavorite]() {
                       if (isFavorite) {
                           setItemFavorite(item, false);
                           emit itemUnstared(item);
                       } else {
                           setItemFavorite(item, true);
                           emit itemStared(item);
                       }
                   });
    menu.addAction(QIcon(QStringLiteral(":/resources/resources/delete.svg")), tr("Delete"), [this, item]() {
        if (category == MPasteSettings::STAR_CATEGORY_NAME) {
            emit itemUnstared(item);
            return;
        }
        removeItemByContent(item);
    });

    menu.exec(listView_->viewport()->mapToGlobal(pos));
}

QString ScrollItemsWidget::getItemFilePath(const ClipboardItem &item) {
    return QDir::cleanPath(saveDir() + QDir::separator() + item.getName() + ".mpaste");
}

void ScrollItemsWidget::setFirstVisibleItemSelected() {
    if (!proxyModel_ || !listView_) {
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
    if ((!currentKeyword_.isEmpty() || currentTypeFilter_ != ClipboardItem::All) && !pendingLoadFilePaths_.isEmpty()) {
        ensureAllItemsLoaded();
    }

    proxyModel_->setTypeFilter(currentTypeFilter_);
    proxyModel_->setKeyword(currentKeyword_);
    proxyModel_->setAsyncMatchedNames(asyncKeywordMatchedNames_);
    setFirstVisibleItemSelected();
    refreshContentWidthHint();
    emit itemCountChanged(itemCountForDisplay());
}

void ScrollItemsWidget::saveItem(const ClipboardItem &item) {
    checkSaveDir();
    saver->saveToFile(item, getItemFilePath(item));
}

void ScrollItemsWidget::checkSaveDir() {
    QDir dir;
    const QString path = QDir::cleanPath(saveDir());
    if (!dir.exists(path)) {
        dir.mkpath(path);
    }
}

void ScrollItemsWidget::moveItemToFirst(int sourceRow) {
    if (sourceRow < 0 || !boardModel_) {
        return;
    }

    ClipboardItem item = boardModel_->itemAt(sourceRow);
    if (item.getName().isEmpty()) {
        return;
    }

    item.getMimeData();
    item = prepareItemForDisplayAndSave(item);
    saver->removeItem(getItemFilePath(boardModel_->itemAt(sourceRow)));
    saveItem(item);
    boardModel_->updateItem(sourceRow, item);
    boardModel_->moveItemToFront(sourceRow);
    setCurrentProxyIndex(proxyIndexForSourceRow(0));
}

QString ScrollItemsWidget::saveDir() {
    return QDir::cleanPath(MPasteSettings::getInst()->getSaveDir() + QDir::separator() + category);
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

void ScrollItemsWidget::loadNextBatch(int batchSize) {
    if (batchSize <= 0 || pendingLoadFilePaths_.isEmpty()) {
        return;
    }

    const int count = qMin(batchSize, pendingLoadFilePaths_.size());
    for (int i = 0; i < count; ++i) {
        const QString filePath = pendingLoadFilePaths_.takeFirst();
        ClipboardItem item = saver->loadFromFileLight(filePath);
        if (item.getName().isEmpty()) {
            saver->removeItem(filePath);
            if (totalItemCount_ > 0) {
                --totalItemCount_;
            }
            continue;
        }

        appendLoadedItem(filePath, item);
    }

    setFirstVisibleItemSelected();
    emit itemCountChanged(itemCountForDisplay());
    updateContentWidthHint();
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
    if (deferredLoadActive_ || pendingLoadFilePaths_.isEmpty() || !currentKeyword_.isEmpty()
        || currentTypeFilter_ != ClipboardItem::All) {
        return;
    }

    QScrollBar *scrollBar = horizontalScrollbar();
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
        if (pendingLoadFilePaths_.size() == pendingBefore || scrollBar->maximum() > 0) {
            break;
        }
    }
}

int ScrollItemsWidget::itemCountForDisplay() const {
    if (currentKeyword_.isEmpty() && currentTypeFilter_ == ClipboardItem::All) {
        return totalItemCount_;
    }
    return proxyModel_ ? proxyModel_->rowCount() : 0;
}

void ScrollItemsWidget::trimExpiredItems() {
    if (category == MPasteSettings::STAR_CATEGORY_NAME) {
        return;
    }

    const QDateTime cutoff = MPasteSettings::getInst()->historyRetentionCutoff();
    while (!pendingLoadFilePaths_.isEmpty()) {
        const QFileInfo info(pendingLoadFilePaths_.last());
        if (!isExpiredForCutoff(info, cutoff)) {
            break;
        }

        saver->removeItem(pendingLoadFilePaths_.takeLast());
        if (totalItemCount_ > 0) {
            --totalItemCount_;
        }
    }

    while (boardModel_ && boardModel_->rowCount() > 0) {
        const ClipboardItem lastItem = boardModel_->itemAt(boardModel_->rowCount() - 1);
        if (lastItem.getName().isEmpty() || lastItem.getTime() >= cutoff) {
            break;
        }

        saver->removeItem(getItemFilePath(lastItem));
        boardModel_->removeItemAt(boardModel_->rowCount() - 1);
        if (totalItemCount_ > 0) {
            --totalItemCount_;
        }
    }

    setFirstVisibleItemSelected();
}

void ScrollItemsWidget::prepareLoadFromSaveDir() {
    checkSaveDir();
    boardModel_->clear();
    pendingLoadFilePaths_.clear();
    totalItemCount_ = 0;
    selectedItemCache_ = ClipboardItem();

    QDir saveDir(this->saveDir());
    const QFileInfoList fileInfos = saveDir.entryInfoList(QStringList() << "*.mpaste", QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &info : fileInfos) {
        if (LocalSaver::isCurrentFormatFile(info.filePath())) {
            pendingLoadFilePaths_ << info.filePath();
        }
    }

    totalItemCount_ = pendingLoadFilePaths_.size();
    trimExpiredItems();
    updateContentWidthHint();
}

void ScrollItemsWidget::continueDeferredLoad() {
    if (!deferredLoadActive_) {
        return;
    }

    processDeferredLoadedItems();
    if (deferredLoadedItems_.isEmpty() && !deferredLoadThread_ && !pendingLoadFilePaths_.isEmpty()) {
        scheduleDeferredLoadBatch();
    }

    if (deferredLoadedItems_.isEmpty() && !deferredLoadThread_ && pendingLoadFilePaths_.isEmpty()) {
        deferredLoadActive_ = false;
        refreshContentWidthHint();
    }
}

bool ScrollItemsWidget::shouldKeepDeferredLoading() const {
    if (pendingLoadFilePaths_.isEmpty() || !currentKeyword_.isEmpty() || currentTypeFilter_ != ClipboardItem::All) {
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

void ScrollItemsWidget::updateContentWidthHint() {
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
    leftEdgeFadeOverlay_->setGeometry(viewportTopLeft.x(), viewportTopLeft.y(), fadeWidth, viewport->height());
    rightEdgeFadeOverlay_->setGeometry(viewportTopLeft.x() + viewport->width() - fadeWidth,
                                       viewportTopLeft.y(),
                                       fadeWidth,
                                       viewport->height());
    leftEdgeFadeOverlay_->raise();
    rightEdgeFadeOverlay_->raise();
    leftEdgeFadeOverlay_->show();
    rightEdgeFadeOverlay_->show();
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

    QPointer<ScrollItemsWidget> guard(this);
    deferredLoadThread_ = QThread::create([guard, batchPaths]() {
        QList<QPair<QString, QByteArray>> batchPayloads;
        batchPayloads.reserve(batchPaths.size());
        for (const QString &filePath : batchPaths) {
            QByteArray rawData;
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly)) {
                rawData = file.readAll();
                file.close();
            }
            batchPayloads.append(qMakePair(filePath, rawData));
        }
        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, batchPayloads]() {
                if (guard) {
                    guard->handleDeferredBatchRead(batchPayloads);
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

void ScrollItemsWidget::handleDeferredBatchRead(const QList<QPair<QString, QByteArray>> &batchPayloads) {
    if (!batchPayloads.isEmpty()) {
        deferredLoadedItems_.append(batchPayloads);
    }

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

    while (!deferredLoadedItems_.isEmpty() && processedCount < maxItemsPerTick && parseTimer.elapsed() < maxParseMs) {
        const auto payload = deferredLoadedItems_.takeFirst();
        const QString filePath = payload.first;
        const QByteArray rawData = payload.second;
        ClipboardItem item = rawData.isEmpty() ? ClipboardItem() : saver->loadFromRawDataLight(rawData, filePath);
        if (item.getName().isEmpty()) {
            saver->removeItem(filePath);
            if (totalItemCount_ > 0) {
                --totalItemCount_;
            }
        } else {
            appendLoadedItem(filePath, item);
        }
        ++processedCount;
    }

    if (!deferredLoadedItems_.isEmpty() && (widgetVisible || processedCount > 0)) {
        deferredLoadTimer_->start(widgetVisible ? 0 : 8);
    }

    setFirstVisibleItemSelected();
    emit itemCountChanged(itemCountForDisplay());
    updateContentWidthHint();
}

void ScrollItemsWidget::waitForDeferredRead() {
    if (deferredLoadThread_) {
        deferredLoadThread_->wait();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

void ScrollItemsWidget::processPendingItemAsync(const ClipboardItem &item, const QString &targetName) {
    const QString expectedName = targetName.isEmpty() ? item.getName() : targetName;
    const ClipboardItem::ContentType contentType = item.getContentType();
    const QByteArray imageBytes = (contentType == ClipboardItem::Image || contentType == ClipboardItem::RichText)
        ? item.imagePayloadBytesFast()
        : QByteArray();
    const QString richHtml = contentType == ClipboardItem::RichText ? item.getHtml() : QString();
    const QSize imageSize = item.isMimeDataLoaded() && contentType == ClipboardItem::Image
        ? item.getImagePixelSize()
        : QSize();
    const QString sourceFilePath = item.sourceFilePath();
    const quint64 mimeOffset = item.mimeDataFileOffset();
    const qreal thumbnailDpr = maxScreenDevicePixelRatio();
    QPointer<ScrollItemsWidget> guard(this);

    QThread *thread = QThread::create([guard, expectedName, contentType, imageBytes, richHtml, imageSize, sourceFilePath, mimeOffset, thumbnailDpr]() mutable {
        PendingItemProcessingResult result;
        QByteArray resolvedImageBytes = imageBytes;
        QString resolvedHtml = richHtml;
        if ((resolvedImageBytes.isEmpty() || resolvedHtml.isEmpty())
            && !sourceFilePath.isEmpty()
            && (contentType == ClipboardItem::Image || contentType == ClipboardItem::RichText)) {
            QString htmlPayload;
            QByteArray imagePayload;
            LocalSaver::loadMimePayloads(sourceFilePath,
                                         mimeOffset,
                                         contentType == ClipboardItem::RichText ? &htmlPayload : nullptr,
                                         (contentType == ClipboardItem::Image || contentType == ClipboardItem::RichText) ? &imagePayload : nullptr);
            if (resolvedHtml.isEmpty()) {
                resolvedHtml = htmlPayload;
            }
            if (resolvedImageBytes.isEmpty()) {
                resolvedImageBytes = imagePayload;
            }
        }

        if (contentType == ClipboardItem::Image && !resolvedImageBytes.isEmpty()) {
            result.thumbnailImage = buildCardThumbnailImageFromBytes(resolvedImageBytes, thumbnailDpr);
            if (isVeryTallImage(imageSize)) {
                qInfo().noquote() << QStringLiteral("[thumb-build] stage=worker name=%1 image=%2x%3 thumbPx=%4x%5 thumbDpr=%6")
                    .arg(expectedName)
                    .arg(imageSize.width())
                    .arg(imageSize.height())
                    .arg(result.thumbnailImage.width())
                    .arg(result.thumbnailImage.height())
                    .arg(result.thumbnailImage.devicePixelRatio(), 0, 'f', 2);
            }
        } else if (contentType == ClipboardItem::RichText && !resolvedHtml.isEmpty()) {
            result.thumbnailImage = buildRichTextThumbnailImageFromHtml(resolvedHtml, resolvedImageBytes, thumbnailDpr);
        }

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, expectedName, result, thumbnailDpr]() mutable {
                if (!guard) {
                    return;
                }

                const int sourceRow = guard->boardModel_ ? guard->boardModel_->rowForName(expectedName) : -1;
                if (sourceRow < 0) {
                    return;
                }

                ClipboardItem preparedItem = guard->boardModel_->itemAt(sourceRow);
                if (!result.thumbnailImage.isNull()) {
                    QPixmap thumbnail = QPixmap::fromImage(result.thumbnailImage);
                    thumbnail.setDevicePixelRatio(qMax<qreal>(1.0, thumbnailDpr));
                    preparedItem.setThumbnail(thumbnail);
                    const QSize imageSize = preparedItem.getImagePixelSize();
                    if (isVeryTallImage(imageSize)) {
                        qInfo().noquote() << QStringLiteral("[thumb-build] stage=ui name=%1 image=%2x%3 thumbPx=%4x%5 thumbLogical=%6x%7 thumbDpr=%8")
                            .arg(expectedName)
                            .arg(imageSize.width())
                            .arg(imageSize.height())
                            .arg(thumbnail.width())
                            .arg(thumbnail.height())
                            .arg(qRound(thumbnail.width() / qMax<qreal>(1.0, thumbnail.devicePixelRatio())))
                            .arg(qRound(thumbnail.height() / qMax<qreal>(1.0, thumbnail.devicePixelRatio())))
                            .arg(thumbnail.devicePixelRatio(), 0, 'f', 2);
                    }
                }
                guard->saveItem(preparedItem);

                ClipboardItem reloadedItem = guard->saver->loadFromFileLight(guard->getItemFilePath(preparedItem));
                if (reloadedItem.getName().isEmpty() || reloadedItem.getName() != expectedName) {
                    return;
                }

                const int row = guard->boardModel_->rowForName(expectedName);
                if (row >= 0) {
                    guard->boardModel_->updateItem(row, reloadedItem);
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

void ScrollItemsWidget::startAsyncKeywordSearch() {
    if (currentKeyword_.isEmpty()) {
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
    const QString keyword = currentKeyword_;
    QPointer<ScrollItemsWidget> guard(this);

    QThread *thread = QThread::create([guard, candidates, keyword, token]() {
        QSet<QString> matchedNames;
        for (const auto &candidate : candidates) {
            if (candidate.first.isEmpty()) {
                continue;
            }
            if (LocalSaver::mimeSectionContainsKeyword(candidate.first, candidate.second, keyword)) {
                const QFileInfo info(candidate.first);
                matchedNames.insert(info.completeBaseName());
            }
        }

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, matchedNames, token]() {
                if (!guard || guard->keywordSearchToken_ != token) {
                    return;
                }

                guard->asyncKeywordMatchedNames_ = matchedNames;
                guard->applyFilters();
            }, Qt::QueuedConnection);
        }
    });

    connect(thread, &QThread::finished, this, [this, thread]() {
        if (keywordSearchThread_ == thread) {
            keywordSearchThread_ = nullptr;
        }
        processingThreads_.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    keywordSearchThread_ = thread;
    processingThreads_.append(thread);
    thread->start();
}

bool ScrollItemsWidget::appendLoadedItem(const QString &filePath, const ClipboardItem &item) {
    int existingRow = boardModel_->rowForMatchingItem(item);
    if (existingRow < 0) {
        existingRow = boardModel_->rowForFingerprint(item.fingerprint());
    }
    if (existingRow >= 0) {
        saver->removeItem(filePath);
        if (totalItemCount_ > 0) {
            --totalItemCount_;
        }
        return false;
    }

    const bool favorite = category == MPasteSettings::STAR_CATEGORY_NAME
        || favoriteFingerprints_.contains(item.fingerprint());
    boardModel_->appendItem(item, favorite);
    if (!item.hasThumbnail()
        && (item.getContentType() == ClipboardItem::RichText || item.getContentType() == ClipboardItem::Image)) {
        processPendingItemAsync(item, item.getName());
    }
    return true;
}

QPair<int, int> ScrollItemsWidget::displaySequenceForIndex(const QModelIndex &proxyIndex) const {
    if (!proxyIndex.isValid() || !proxyModel_) {
        return qMakePair(-1, itemCountForDisplay());
    }
    return qMakePair(proxyIndex.row() + 1, proxyModel_->rowCount());
}

int ScrollItemsWidget::selectedSourceRow() const {
    const QModelIndex proxyIndex = currentProxyIndex();
    if (!proxyIndex.isValid() || !proxyModel_) {
        return -1;
    }
    return proxyModel_->mapToSource(proxyIndex).row();
}

const ClipboardItem *ScrollItemsWidget::cacheSelectedItem(int sourceRow) const {
    if (!boardModel_ || sourceRow < 0) {
        return nullptr;
    }

    selectedItemCache_ = boardModel_->itemAt(sourceRow);
    return selectedItemCache_.getName().isEmpty() ? nullptr : &selectedItemCache_;
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
            saver->removeItem(getItemFilePath(existingItem));
            boardModel_->updateItem(row, nItem);
            if (!nItem.isMimeDataLoaded() && nItem.sourceFilePath().isEmpty()) {
                processPendingItemAsync(nItem, nItem.getName());
            } else {
                saveItem(nItem);
            }
        }
        if (row > 0) {
            moveItemToFirst(row);
        }
        return false;
    }

    const bool favorite = category == MPasteSettings::STAR_CATEGORY_NAME
        || favoriteFingerprints_.contains(nItem.fingerprint());
    boardModel_->prependItem(nItem, favorite);
    const QModelIndex firstProxyIndex = proxyIndexForSourceRow(0);
    setCurrentProxyIndex(firstProxyIndex);
    ensureLinkPreviewForIndex(firstProxyIndex);

    ++totalItemCount_;
    trimExpiredItems();
    refreshContentWidthHint();
    emit itemCountChanged(itemCountForDisplay());
    return true;
}

bool ScrollItemsWidget::addAndSaveItem(const ClipboardItem &nItem) {
    const bool isPendingClipboardSnapshot = !nItem.isMimeDataLoaded() && nItem.sourceFilePath().isEmpty();
    ClipboardItem preparedItem = isPendingClipboardSnapshot ? nItem : prepareItemForDisplayAndSave(nItem);
    const bool added = addOneItem(preparedItem);
    ensureLinkPreviewForIndex(proxyIndexForSourceRow(0));
    if (added) {
        if (isPendingClipboardSnapshot) {
            processPendingItemAsync(preparedItem, preparedItem.getName());
        } else {
            saveItem(preparedItem);
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

void ScrollItemsWidget::setShortcutInfo() {
    cleanShortCutInfo();
    if (!proxyModel_) {
        return;
    }

    for (int row = 0; row < proxyModel_->rowCount() && row < 10; ++row) {
        const QModelIndex proxyIndex = proxyModel_->index(row, 0);
        const int sourceRow = proxyModel_->mapToSource(proxyIndex).row();
        boardModel_->setShortcutText(sourceRow, QStringLiteral("Alt+%1").arg((row + 1) % 10));
    }
}

void ScrollItemsWidget::cleanShortCutInfo() {
    if (boardModel_) {
        boardModel_->clearShortcutTexts();
    }
}

void ScrollItemsWidget::loadFromSaveDir() {
    deferredLoadActive_ = false;
    deferredLoadTimer_->stop();
    prepareLoadFromSaveDir();
    loadNextBatch(INITIAL_LOAD_BATCH_SIZE);
    maybeLoadMoreItems();
}

void ScrollItemsWidget::loadFromSaveDirDeferred() {
    deferredLoadActive_ = true;
    deferredLoadTimer_->stop();
    prepareLoadFromSaveDir();

    if (pendingLoadFilePaths_.isEmpty()) {
        deferredLoadActive_ = false;
        emit itemCountChanged(itemCountForDisplay());
        return;
    }

    loadNextBatch(qMin(DEFERRED_LOAD_BATCH_SIZE, INITIAL_LOAD_BATCH_SIZE));
    if (shouldKeepDeferredLoading()) {
        scheduleDeferredLoadBatch();
    } else {
        deferredLoadActive_ = false;
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
    if (!proxyModel_ || keyIndex < 0 || keyIndex >= proxyModel_->rowCount()) {
        return currentSelectedItem();
    }

    const QModelIndex proxyIndex = proxyModel_->index(keyIndex, 0);
    setCurrentProxyIndex(proxyIndex);
    moveItemToFirst(proxyModel_->mapToSource(proxyIndex).row());
    return cacheSelectedItem(0);
}

const ClipboardItem *ScrollItemsWidget::selectedByEnter() {
    const int sourceRow = selectedSourceRow();
    if (sourceRow < 0) {
        return nullptr;
    }

    moveItemToFirst(sourceRow);
    return cacheSelectedItem(0);
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

void ScrollItemsWidget::removeItemByContent(const ClipboardItem &item) {
    int row = boardModel_->rowForMatchingItem(item);
    if (row < 0) {
        row = boardModel_->rowForFingerprint(item.fingerprint());
    }
    if (row >= 0) {
        saver->removeItem(getItemFilePath(boardModel_->itemAt(row)));
        boardModel_->removeItemAt(row);
        if (totalItemCount_ > 0) {
            --totalItemCount_;
        }
        setFirstVisibleItemSelected();
        maybeLoadMoreItems();
        refreshContentWidthHint();
        emit itemCountChanged(itemCountForDisplay());
        return;
    }

    const QString filePath = getItemFilePath(item);
    const int pendingIndex = pendingLoadFilePaths_.indexOf(filePath);
    if (pendingIndex >= 0) {
        pendingLoadFilePaths_.removeAt(pendingIndex);
        saver->removeItem(filePath);
        if (totalItemCount_ > 0) {
            --totalItemCount_;
        }
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
    boardModel_->setFavoriteByFingerprint(item.fingerprint(), favorite);
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

void ScrollItemsWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    updateEdgeFadeOverlays();
    if (!deferredLoadedItems_.isEmpty() && !deferredLoadTimer_->isActive()) {
        deferredLoadTimer_->start(0);
    }
}

void ScrollItemsWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    refreshContentWidthHint();
}

bool ScrollItemsWidget::eventFilter(QObject *watched, QEvent *event) {
    if (!listView_) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::Wheel && (watched == listView_ || watched == listView_->viewport())) {
        const bool handled = handleWheelScroll(static_cast<QWheelEvent *>(event));
        if (handled) {
            updateHoverActionBarPosition();
        }
        return handled;
    }

    if (event->type() == QEvent::Resize && watched == listView_->viewport()) {
        refreshContentWidthHint();
        updateHoverActionBarPosition();
    }

    if ((watched == listView_ || watched == listView_->viewport()) && event->type() == QEvent::MouseMove) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (hoverActionBar_ && hoverActionBar_->isVisible() && hoverActionBar_->geometry().contains(mouseEvent->pos())) {
            return QWidget::eventFilter(watched, event);
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
        QGuiApplication::sendEvent(parent(), event);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
