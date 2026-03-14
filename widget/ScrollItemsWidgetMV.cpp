// input: Depends on ScrollItemsWidget.h, ClipboardBoardService, Qt model/view APIs, and delegate-based card painting.
// output: Implements lazy-loaded boards, proxy filtering, async thumbnail completion, and list-view item interaction.
// pos: Widget-layer board implementation driving clipboard and favorites history lists.
// update: If I change, update this header block and my folder README.md (arrow navigation no longer forces center + hover action bar).
// note: Added dark theme rendering hooks.
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScroller>
#include <QShowEvent>
#include <QToolButton>
#include <QStyle>
#include <QStyleFactory>
#include <QGraphicsOpacityEffect>
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
#include "utils/ThemeManager.h"
#include "utils/IconResolver.h"

namespace {

QString hoverButtonStyle(bool dark, int borderRadius, int padding) {
    const QString hoverColor = dark ? QStringLiteral("rgba(255, 255, 255, 0.12)")
                                    : QStringLiteral("rgba(0, 0, 0, 0.08)");
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
    menu->setStyleSheet(qApp->styleSheet());
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

static DWORD accentColorFromArgb(const QColor &color) {
    return (static_cast<DWORD>(color.alpha()) << 24)
        | (static_cast<DWORD>(color.blue()) << 16)
        | (static_cast<DWORD>(color.green()) << 8)
        | static_cast<DWORD>(color.red());
}

static void enableBlurBehindForWidget(HWND hwnd, const QColor &tintColor) {
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

    DWORD tint = accentColorFromArgb(tintColor);

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
    explicit ClipboardBoardView(QWidget *parent = nullptr)
        : QListView(parent) {}

    void applyViewportMargins(int left, int top, int right, int bottom) {
        setViewportMargins(left, top, right, bottom);
    }

    void refreshItemGeometries() {
        updateGeometries();
    }
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

    connect(boardService_, &ClipboardBoardService::itemsLoaded, this, &ScrollItemsWidget::handleLoadedItems);
    connect(boardService_, &ClipboardBoardService::pendingItemReady, this, &ScrollItemsWidget::handlePendingItemReady);
    connect(boardService_, &ClipboardBoardService::keywordMatched, this, &ScrollItemsWidget::handleKeywordMatched);
    connect(boardService_, &ClipboardBoardService::totalItemCountChanged, this, &ScrollItemsWidget::handleTotalItemCountChanged);
    connect(boardService_, &ClipboardBoardService::deferredLoadCompleted, this, &ScrollItemsWidget::handleDeferredLoadCompleted);

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

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ScrollItemsWidget::applyTheme);
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

#ifdef Q_OS_WIN
    if (hoverActionBar_) {
        const QColor tint = darkTheme_ ? QColor(16, 22, 30, 72) : QColor(255, 255, 255, 28);
        enableBlurBehindForWidget(reinterpret_cast<HWND>(hoverActionBar_->winId()), tint);
    }
#endif

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
    if (!listView_) {
        return;
    }

    const int scale = MPasteSettings::getInst()->getItemScale();
    const int barHeight = qMax(20, 28 * scale / 100);
    const int borderRadius = qMax(6, 8 * scale / 100);
    const int margin = qMax(4, 6 * scale / 100);
    const int spacing = qMax(2, 4 * scale / 100);
    const bool dark = ThemeManager::instance()->isDark();

    auto *hoverBar = new HoverActionBar(listView_->viewport());
    hoverActionBar_ = hoverBar;
    hoverActionBar_->setObjectName(QStringLiteral("cardActionBar"));
    hoverActionBar_->setFixedHeight(barHeight);
    hoverActionBar_->setFocusPolicy(Qt::NoFocus);
    hoverActionBar_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    hoverActionBar_->setAttribute(Qt::WA_TranslucentBackground);
    hoverBar->setCornerRadius(borderRadius);
    hoverBar->setColors(dark ? QColor(26, 31, 38, 205) : QColor(255, 255, 255, 185),
                        dark ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 120));

#ifdef Q_OS_WIN
    hoverActionBar_->setAttribute(Qt::WA_NativeWindow);
    const QColor tint = dark ? QColor(16, 22, 30, 72) : QColor(255, 255, 255, 28);
    enableBlurBehindForWidget(reinterpret_cast<HWND>(hoverActionBar_->winId()), tint);
#endif

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
    hoverFavoriteBtn_ = createButton(IconResolver::themedPath(QStringLiteral("star_outline"), dark), tr("Add to favorites"));
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
    if (favorite) {
        hoverFavoriteBtn_->setIcon(QIcon(QStringLiteral(":/resources/resources/star_filled.svg")));
    } else {
        hoverFavoriteBtn_->setIcon(IconResolver::themedIcon(QStringLiteral("star_outline"), darkTheme_));
    }
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

    setCurrentProxyIndex(proxyIndex);
    const int sourceRow = proxyModel_->mapToSource(proxyIndex).row();
    const ClipboardItem item = boardModel_->itemAt(sourceRow);
    if (item.getName().isEmpty()) {
        return;
    }

    const bool dark = darkTheme_;

    const bool isFavorite = boardModel_->isFavorite(sourceRow);
    const QList<QUrl> urls = item.getNormalizedUrls();
    const bool canOpenFolder = item.getContentType() == ClipboardItem::File
        && !urls.isEmpty()
        && std::all_of(urls.begin(), urls.end(), [](const QUrl &url) { return url.isLocalFile(); });

    QMenu menu(this);
    applyMenuTheme(&menu);
    menu.addAction(IconResolver::themedIcon(QStringLiteral("text_plain"), dark), plainTextPasteLabel(), [this]() {
        const ClipboardItem *selectedItem = selectedByEnter();
        if (selectedItem) {
            emit plainTextPasteRequested(*selectedItem);
        }
    });
    menu.addAction(IconResolver::themedIcon(QStringLiteral("details"), dark), detailsLabel(), [this, proxyIndex]() {
        setCurrentProxyIndex(proxyIndex);
        const ClipboardItem *selectedItem = currentSelectedItem();
        if (!selectedItem) {
            return;
        }
        const QPair<int, int> sequenceInfo = displaySequenceForIndex(proxyIndex);
        emit detailsRequested(*selectedItem, sequenceInfo.first, sequenceInfo.second);
    });
    if (ClipboardItemPreviewDialog::supportsPreview(item)) {
        menu.addAction(IconResolver::themedIcon(QStringLiteral("preview"), dark), tr("Preview"), [this]() {
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

void ScrollItemsWidget::handleLoadedItems(const QList<QPair<QString, ClipboardItem>> &items) {
    if (items.isEmpty() || !boardModel_) {
        return;
    }

    for (const auto &payload : items) {
        appendLoadedItem(payload.first, payload.second);
    }

    setFirstVisibleItemSelected();
    emit itemCountChanged(itemCountForDisplay());
    updateContentWidthHint();
}

void ScrollItemsWidget::handlePendingItemReady(const QString &expectedName, const ClipboardItem &item) {
    if (!boardModel_ || expectedName.isEmpty()) {
        return;
    }

    const int row = boardModel_->rowForName(expectedName);
    if (row >= 0) {
        boardModel_->updateItem(row, item);
    }
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
    refreshContentWidthHint();
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
    if ((!currentKeyword_.isEmpty() || currentTypeFilter_ != ClipboardItem::All)
        && boardService_ && boardService_->hasPendingItems()) {
        ensureAllItemsLoaded();
    }

    proxyModel_->setTypeFilter(currentTypeFilter_);
    proxyModel_->setKeyword(currentKeyword_);
    proxyModel_->setAsyncMatchedNames(asyncKeywordMatchedNames_);
    setFirstVisibleItemSelected();
    refreshContentWidthHint();
    emit itemCountChanged(itemCountForDisplay());
}

void ScrollItemsWidget::moveItemToFirst(int sourceRow) {
    if (sourceRow < 0 || !boardModel_ || !boardService_) {
        return;
    }

    ClipboardItem item = boardModel_->itemAt(sourceRow);
    if (item.getName().isEmpty()) {
        return;
    }

    item.getMimeData();
    item = boardService_->prepareItemForSave(item);
    boardService_->removeItemFile(boardService_->filePathForItem(boardModel_->itemAt(sourceRow)));
    boardService_->saveItem(item);
    boardModel_->updateItem(sourceRow, item);
    boardModel_->moveItemToFront(sourceRow);
    setCurrentProxyIndex(proxyIndexForSourceRow(0));
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
    boardService_->refreshIndex();
    updateContentWidthHint();
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
    boardModel_->appendItem(item, favorite);
    if (!item.hasThumbnail()
        && (item.getContentType() == ClipboardItem::RichText || item.getContentType() == ClipboardItem::Image)) {
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
            if (boardService_) {
                boardService_->removeItemFile(boardService_->filePathForItem(existingItem));
            }
            boardModel_->updateItem(row, nItem);
            if (!nItem.isMimeDataLoaded() && nItem.sourceFilePath().isEmpty()) {
                if (boardService_) {
                    boardService_->processPendingItemAsync(nItem, nItem.getName());
                }
            } else {
                if (boardService_) {
                    boardService_->saveItem(nItem);
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
    boardModel_->prependItem(nItem, favorite);
    const QModelIndex firstProxyIndex = proxyIndexForSourceRow(0);
    setCurrentProxyIndex(firstProxyIndex);
    ensureLinkPreviewForIndex(firstProxyIndex);

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
    if (boardService_) {
        boardService_->stopDeferredLoad();
    }
    prepareLoadFromSaveDir();
    if (boardService_) {
        boardService_->loadNextBatch(INITIAL_LOAD_BATCH_SIZE);
    }
    maybeLoadMoreItems();
}

void ScrollItemsWidget::loadFromSaveDirDeferred() {
    if (boardService_) {
        boardService_->stopDeferredLoad();
    }
    prepareLoadFromSaveDir();

    if (!boardService_ || !boardService_->hasPendingItems()) {
        emit itemCountChanged(itemCountForDisplay());
        return;
    }

    boardService_->loadNextBatch(qMin(DEFERRED_LOAD_BATCH_SIZE, INITIAL_LOAD_BATCH_SIZE));
    if (shouldKeepDeferredLoading()) {
        boardService_->startDeferredLoad(DEFERRED_LOAD_BATCH_SIZE);
    } else {
        boardService_->stopDeferredLoad();
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
        if (boardService_) {
            boardService_->deleteItemByPath(boardService_->filePathForItem(boardModel_->itemAt(row)));
        }
        boardModel_->removeItemAt(row);
        setFirstVisibleItemSelected();
        maybeLoadMoreItems();
        refreshContentWidthHint();
        emit itemCountChanged(itemCountForDisplay());
        return;
    }

    if (boardService_) {
        const QString filePath = boardService_->filePathForItem(item);
        if (boardService_->deletePendingItemByPath(filePath)) {
            refreshContentWidthHint();
            emit itemCountChanged(itemCountForDisplay());
        }
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
    if (!scrollBar || (scrollBar->maximum() <= 0 && (!boardService_ || !boardService_->hasPendingItems()))) {
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
    if (boardService_) {
        boardService_->setVisibleHint(isVisible() && window() && window()->isVisible());
    }
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
