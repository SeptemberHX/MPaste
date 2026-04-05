// input: Depends on MPasteWidget.h, Qt runtime services, resource assets, and platform clipboard/window helpers.
// output: Implements the main window, item interaction flow, reliable quick-paste shortcuts, and plain-text paste behavior.
// pos: Widget-layer main window implementation coordinating boards, shortcuts, and system integration.
// update: If I change, update this header block and my folder README.md.
// note: Added theme application, dark mode propagation, tray menu theming, robust paste rehydration, and alias sync.
#include <QScrollBar>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QButtonGroup>
#include <QLocale>
#include <QIcon>
#include <QAction>
#include <QApplication>
#include <QStringList>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QTextDocument>
#include <QWheelEvent>
#include <QWindow>
#include <QPainter>
#include <QPainterPath>
#include <QClipboard>
#include <QFrame>
#include <QMessageBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QToolButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleFactory>
#include "utils/MPasteSettings.h"
#include "utils/ThemeManager.h"
#include "WindowBlurHelper.h"
#include "utils/IconResolver.h"
#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "BoardInternalHelpers.h"
#include "ClipboardCardDelegate.h"
#include "CopySoundPlayer.h"
#include "SyncWatcher.h"
#include "ClipboardPasteController.h"
#include "utils/PlatformRelated.h"
#include "data/LocalSaver.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

// Minimal QDialog subclass that paints a transparent fill so mouse
// events are not passed through the translucent window.
class OcrResultDialog : public QDialog {
public:
    bool dark = false;
    QPoint dragOffset_;
    using QDialog::QDialog;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal r = 8.0;
        QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(dark ? QColor(255,255,255,40) : QColor(0,0,0,25), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect, r, r);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 1));
        p.drawRoundedRect(rect, r, r);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            dragOffset_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
            e->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->buttons() & Qt::LeftButton) {
            move(e->globalPosition().toPoint() - dragOffset_);
            e->accept();
        }
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) { close(); return; }
        QDialog::keyPressEvent(e);
    }
};

namespace {
QString menuText(const char *source, const QString &zhFallback) {
    const QString translated = QObject::tr(source);
    const QLocale locale = QLocale::system();
    if (translated == QLatin1String(source) || BoardHelpers::looksBrokenTranslation(translated)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return zhFallback;
        }
        return QString::fromUtf8(source);
    }
    return translated;
}

class PageSelectorPill final : public QWidget {
public:
    explicit PageSelectorPill(QWidget *parent = nullptr)
        : QWidget(parent) {}

    void setColors(const QColor &background, const QColor &border, const QColor &divider) {
        background_ = background;
        border_ = border;
        divider_ = divider;
        update();
    }

    void setCornerRadius(int radius) {
        cornerRadius_ = radius;
        update();
    }

    void setDividerPosition(int x) {
        dividerX_ = x;
        update();
    }

    void setDividerInset(int inset) {
        dividerInset_ = inset;
        update();
    }

    void setPaintFrame(bool paintFrame) {
        paintFrame_ = paintFrame;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (!paintFrame_) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF rectF = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        painter.setPen(QPen(border_, 2));
        painter.setBrush(background_);
        painter.drawRoundedRect(rectF, cornerRadius_, cornerRadius_);

        if (dividerX_ > 0) {
            painter.setPen(QPen(divider_, 1));
            const int top = qBound(2, dividerInset_, height() / 2);
            const int bottom = qMax(top, height() - dividerInset_ - 1);
            painter.drawLine(QPointF(dividerX_, top), QPointF(dividerX_, bottom));
        }
    }

private:
    QColor background_ = QColor(Qt::white);
    QColor border_ = QColor(Qt::black);
    QColor divider_ = QColor(Qt::black);
    int cornerRadius_ = 14;
    int dividerX_ = 0;
    int dividerInset_ = 3;
    bool paintFrame_ = true;
};

// Glass dropdown popup for the page selector — replaces QComboBox whose
// dropdown fails to render item text on Windows Tool windows.
class GlassPagePopup final : public QWidget {
    Q_OBJECT
public:
    explicit GlassPagePopup(QWidget *parent = nullptr)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
    }

    void setPages(int totalPages, int currentPage) {
        totalPages_ = qMax(1, totalPages);
        currentPage_ = qBound(1, currentPage, totalPages_);
        hoveredPage_ = -1;
        scrollY_ = 0;
        recalcSize();
        // Scroll so the current page is visible.
        ensurePageVisible(currentPage_);
    }

    void popup(const QPoint &globalPos) {
        move(globalPos);
        show();
        WindowBlurHelper::enableBlurBehind(this, dark_);
        raise();
    }

    void setDark(bool dark) { dark_ = dark; }

signals:
    void pageSelected(int page);

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        const QColor bg = dark_ ? QColor(30, 36, 48, 1) : QColor(245, 245, 248, 1);
        const QColor border = dark_ ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 22);
        p.setPen(QPen(border, 1));
        p.setBrush(bg);
        p.drawRoundedRect(r, 8, 8);

        const QColor textColor = dark_ ? QColor(230, 237, 245) : QColor(30, 41, 54);
        const QColor hoverBg = dark_ ? QColor(255, 255, 255, 24) : QColor(0, 0, 0, 14);
        const QColor currentBg = QColor(74, 144, 226, dark_ ? 90 : 60);

        QFont font;
        font.setPixelSize(13);
        font.setWeight(QFont::DemiBold);
        p.setFont(font);

        // Clip to content area to prevent cells from bleeding into rounded corners.
        p.setClipRect(QRect(kPad, kPad, width() - kPad * 2, height() - kPad * 2));

        const int cols = columns();
        for (int i = 0; i < totalPages_; ++i) {
            const int page = i + 1;
            const QRect cell = cellRect(i, cols);
            if (cell.bottom() < kPad || cell.top() > height() - kPad) {
                continue; // Off-screen — skip.
            }
            if (page == currentPage_) {
                p.setPen(Qt::NoPen);
                p.setBrush(currentBg);
                p.drawRoundedRect(cell.adjusted(1, 1, -1, -1), 6, 6);
            } else if (page == hoveredPage_) {
                p.setPen(Qt::NoPen);
                p.setBrush(hoverBg);
                p.drawRoundedRect(cell.adjusted(1, 1, -1, -1), 6, 6);
            }
            p.setPen(textColor);
            p.drawText(cell, Qt::AlignCenter, QString::number(page));
        }

        // Scrollbar indicator when content overflows.
        if (needsScroll()) {
            p.setClipping(false);
            const int totalContentH = totalRows() * kCellH;
            const int viewH = visibleHeight();
            const int trackH = viewH - 4;
            const int thumbH = qMax(12, trackH * viewH / totalContentH);
            const int thumbY = kPad + 2 + (trackH - thumbH) * scrollY_ / maxScroll();
            const int trackX = width() - kPad;
            p.setPen(Qt::NoPen);
            p.setBrush(dark_ ? QColor(255, 255, 255, 36) : QColor(0, 0, 0, 28));
            p.drawRoundedRect(QRectF(trackX, thumbY, 3, thumbH), 1.5, 1.5);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        const int page = pageAtPos(event->pos());
        if (page != hoveredPage_) {
            hoveredPage_ = page;
            update();
        }
    }

    void mousePressEvent(QMouseEvent *event) override {
        const int page = pageAtPos(event->pos());
        if (page > 0) {
            emit pageSelected(page);
        }
        close();
    }

    void wheelEvent(QWheelEvent *event) override {
        if (!needsScroll()) return;
        const int delta = event->angleDelta().y();
        scrollY_ = qBound(0, scrollY_ - (delta > 0 ? kCellH : -kCellH), maxScroll());
        hoveredPage_ = pageAtPos(mapFromGlobal(QCursor::pos()));
        update();
        event->accept();
    }

    void leaveEvent(QEvent *) override {
        hoveredPage_ = -1;
        update();
    }

private:
    static constexpr int kCellW = 38;
    static constexpr int kCellH = 30;
    static constexpr int kPad = 6;
    static constexpr int kMaxVisibleRows = 4;

    int columns() const {
        if (totalPages_ <= 5) return qMax(1, totalPages_);
        if (totalPages_ <= 20) return 5;
        return 6;
    }

    int totalRows() const {
        const int cols = columns();
        return (totalPages_ + cols - 1) / cols;
    }

    int visibleHeight() const {
        return qMin(totalRows(), kMaxVisibleRows) * kCellH;
    }

    bool needsScroll() const { return totalRows() > kMaxVisibleRows; }

    int maxScroll() const {
        return qMax(0, totalRows() * kCellH - visibleHeight());
    }

    void recalcSize() {
        const int cols = columns();
        const int scrollbarW = needsScroll() ? 5 : 0;
        resize(cols * kCellW + kPad * 2 + scrollbarW,
               visibleHeight() + kPad * 2);
    }

    void ensurePageVisible(int page) {
        if (!needsScroll()) return;
        const int cols = columns();
        const int row = (page - 1) / cols;
        const int rowTop = row * kCellH;
        const int rowBot = rowTop + kCellH;
        const int viewH = visibleHeight();
        if (rowTop < scrollY_) {
            scrollY_ = rowTop;
        } else if (rowBot > scrollY_ + viewH) {
            scrollY_ = rowBot - viewH;
        }
        scrollY_ = qBound(0, scrollY_, maxScroll());
    }

    QRect cellRect(int index, int cols) const {
        const int col = index % cols;
        const int row = index / cols;
        return QRect(kPad + col * kCellW,
                     kPad + row * kCellH - scrollY_,
                     kCellW, kCellH);
    }

    int pageAtPos(const QPoint &pos) const {
        const int cols = columns();
        const int col = (pos.x() - kPad) / kCellW;
        const int row = (pos.y() - kPad + scrollY_) / kCellH;
        if (col < 0 || col >= cols || row < 0
            || pos.x() < kPad || pos.y() < kPad
            || pos.x() > width() - kPad || pos.y() > height() - kPad) {
            return -1;
        }
        const int index = row * cols + col;
        return (index >= 0 && index < totalPages_) ? index + 1 : -1;
    }

    int totalPages_ = 1;
    int currentPage_ = 1;
    int hoveredPage_ = -1;
    int scrollY_ = 0;
    bool dark_ = false;
};

ClipboardItem rehydrateClipboardItem(const ClipboardItem &item) {
    if (item.getName().isEmpty()) {
        return {};
    }

    LocalSaver saver;
    const QString sourceFilePath = item.sourceFilePath();
    if (!sourceFilePath.isEmpty() && QFileInfo::exists(sourceFilePath)) {
        ClipboardItem loaded = saver.loadFromFile(sourceFilePath);
        if (!loaded.getName().isEmpty()) {
            return loaded;
        }
    }

    const QString rootDir = QDir::cleanPath(MPasteSettings::getInst()->getSaveDir());
    if (rootDir.isEmpty()) {
        return {};
    }

    const QStringList categories = {
        MPasteSettings::STAR_CATEGORY_NAME,
        MPasteSettings::CLIPBOARD_CATEGORY_NAME
    };
    for (const QString &category : categories) {
        const QString filePath = QDir::cleanPath(rootDir + QDir::separator()
                                                 + category + QDir::separator()
                                                 + item.getName() + ".mpaste");
        if (!QFileInfo::exists(filePath)) {
            continue;
        }
        ClipboardItem loaded = saver.loadFromFile(filePath);
        if (!loaded.getName().isEmpty()) {
            return loaded;
        }
    }

    return {};
}

bool hasUsableMimeData(ClipboardItem item) {
    const QMimeData *mimeData = item.getMimeData();
    return mimeData && !mimeData->formats().isEmpty();
}

QString elideClipboardLogText(QString text, int maxLen = 48) {
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    if (text.size() > maxLen) {
        text.truncate(maxLen);
        text.append(QStringLiteral("..."));
    }
    return text;
}

QString widgetItemSummary(const ClipboardItem &item) {
    return QStringLiteral("type=%1 fp=%2 text=\"%3\" htmlLen=%4 urlCount=%5")
        .arg(item.getContentType())
        .arg(QString::fromLatin1(item.fingerprint().toHex().left(12)))
        .arg(elideClipboardLogText(item.getNormalizedText()))
        .arg(item.getHtml().size())
        .arg(item.getNormalizedUrls().size());
}

}

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>

// Windows 11 backdrop types (DWMWA_SYSTEMBACKDROP_TYPE = 38)
enum DWM_SYSTEMBACKDROP_TYPE {
    DWMSBT_AUTO            = 0,
    DWMSBT_NONE            = 1,
    DWMSBT_MAINWINDOW      = 2, // Mica
    DWMSBT_TRANSIENTWINDOW = 3, // Acrylic
    DWMSBT_TABBEDWINDOW    = 4  // Mica Alt
};

// Undocumented but stable blur-behind API
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

static void enableBlurBehind(HWND hwnd, const QColor &tintColor) {
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    DWORD preference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));

    // Acrylic blur via SetWindowCompositionAttribute (instant, no flicker)
    auto user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto setWCA = reinterpret_cast<pfnSetWindowCompositionAttribute>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!setWCA) return;

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

// QEvent::KeyPress conflicts with the KeyPress in X.h
#undef KeyPress

static int shortcutIndexForKey(int key) {
    switch (key) {
        case Qt::Key_1:
        case Qt::Key_Exclam:
            return 0;
        case Qt::Key_2:
        case Qt::Key_At:
            return 1;
        case Qt::Key_3:
        case Qt::Key_NumberSign:
            return 2;
        case Qt::Key_4:
        case Qt::Key_Dollar:
            return 3;
        case Qt::Key_5:
        case Qt::Key_Percent:
            return 4;
        case Qt::Key_6:
        case Qt::Key_AsciiCircum:
            return 5;
        case Qt::Key_7:
        case Qt::Key_Ampersand:
            return 6;
        case Qt::Key_8:
        case Qt::Key_Asterisk:
            return 7;
        case Qt::Key_9:
        case Qt::Key_ParenLeft:
            return 8;
        case Qt::Key_0:
        case Qt::Key_ParenRight:
            return 9;
        default:
            return -1;
    }
}

MPasteWidget::MPasteWidget(QWidget *parent) :
    QWidget(parent)
{
    ui_.ui = new Ui::MPasteWidget;
    ui_.ui->setupUi(this);
    clipboard_.copiedWhenHide = false;
    initializeWidget();
}

MPasteWidget::~MPasteWidget() {
    delete ui_.ui;
}

void MPasteWidget::initializeWidget() {
    misc_.startupPerfTimer.start();
    qInfo() << "[startup] initializeWidget begin";

    initStyle();
    qInfo().noquote() << QStringLiteral("[startup] initStyle done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    initUI();
    qInfo().noquote() << QStringLiteral("[startup] initUI done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    initClipboard();
    qInfo().noquote() << QStringLiteral("[startup] initClipboard done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    initShortcuts();
    qInfo().noquote() << QStringLiteral("[startup] initShortcuts done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    initSystemTray();
    qInfo().noquote() << QStringLiteral("[startup] initSystemTray done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    initSound();
    qInfo().noquote() << QStringLiteral("[startup] initSound done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    setupConnections();
    qInfo().noquote() << QStringLiteral("[startup] setupConnections done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    setupSyncWatcher();
    scheduleStartupWarmup();
    qInfo().noquote() << QStringLiteral("[startup] startup warmup scheduled elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());

    setFocusOnSearch(false);
    misc_.pendingNumKey = 0;
    qInfo().noquote() << QStringLiteral("[startup] initializeWidget end totalElapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    QTimer::singleShot(0, this, [this]() {
        qInfo().noquote() << QStringLiteral("[startup] event-loop checkpoint 0ms elapsedMs=%1 visible=%2 active=%3")
            .arg(misc_.startupPerfTimer.elapsed())
            .arg(isVisible())
            .arg(isActiveWindow());
    });
    QTimer::singleShot(100, this, [this]() {
        qInfo().noquote() << QStringLiteral("[startup] event-loop checkpoint 100ms elapsedMs=%1 visible=%2 active=%3")
            .arg(misc_.startupPerfTimer.elapsed())
            .arg(isVisible())
            .arg(isActiveWindow());
    });
    QTimer::singleShot(500, this, [this]() {
        qInfo().noquote() << QStringLiteral("[startup] event-loop checkpoint 500ms elapsedMs=%1 visible=%2 active=%3")
            .arg(misc_.startupPerfTimer.elapsed())
            .arg(isVisible())
            .arg(isActiveWindow());
    });

#ifdef _DEBUG
    QTimer* debugTimer = new QTimer(this);
    connect(debugTimer, &QTimer::timeout, this, &MPasteWidget::debugKeyState);
    debugTimer->start(1000);
#endif
}

void MPasteWidget::initStyle() {
    Qt::WindowFlags flags = Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::Tool | Qt::FramelessWindowHint;

#ifdef Q_OS_LINUX
    flags |= Qt::X11BypassWindowManagerHint;
    flags |= Qt::Window;
#endif
    setWindowFlags(flags);

    setFocusPolicy(Qt::StrongFocus);

#ifdef Q_OS_WIN
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setAttribute(Qt::WA_KeyCompression, false);
    setAttribute(Qt::WA_NoSystemBackground);
#else
    setAttribute(Qt::WA_TranslucentBackground);
#endif

    setAttribute(Qt::WA_AlwaysStackOnTop);
    ui_.ui->itemsWidget->setAttribute(Qt::WA_TranslucentBackground);
    ui_.ui->itemsWidget->setAttribute(Qt::WA_NoSystemBackground, false);

    setObjectName("pasteWidget");
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &MPasteWidget::applyTheme);
}

void MPasteWidget::initUI() {
    initSearchAnimations();

    ui_.clipboardWidget = new ScrollItemsWidget(
        MPasteSettings::CLIPBOARD_CATEGORY_NAME, MPasteSettings::CLIPBOARD_CATEGORY_COLOR, this);
    ui_.ui->clipboardButton->setProperty("category", ui_.clipboardWidget->getCategory());
    ui_.clipboardWidget->installEventFilter(this);
    ui_.boardWidgetMap.insert(ui_.clipboardWidget->getCategory(), ui_.clipboardWidget);

    ui_.staredWidget = new ScrollItemsWidget(
        MPasteSettings::STAR_CATEGORY_NAME, MPasteSettings::STAR_CATEGORY_COLOR, this);
    ui_.ui->staredButton->setProperty("category", ui_.staredWidget->getCategory());
    ui_.staredWidget->installEventFilter(this);
    ui_.boardWidgetMap.insert(ui_.staredWidget->getCategory(), ui_.staredWidget);

    ui_.layout = new QHBoxLayout(ui_.ui->itemsWidget);
    ui_.layout->setContentsMargins(0, 10, 0, 0);
    ui_.layout->setSpacing(0);
    ui_.layout->addWidget(ui_.clipboardWidget);
    ui_.layout->addWidget(ui_.staredWidget);

    ui_.staredWidget->hide();

    ui_.ui->clipboardButton->setText(menuText("Clipboard", QStringLiteral("剪贴板")));
    ui_.ui->staredButton->setText(menuText("Stared", QStringLiteral("收藏夹")));
    ui_.ui->allTypeBtn->setText(menuText("All", QStringLiteral("全部")));
    ui_.ui->textTypeBtn->setText(menuText("Text", QStringLiteral("文本")));
    ui_.ui->linkTypeBtn->setText(menuText("Link", QStringLiteral("链接")));
    ui_.ui->imageTypeBtn->setText(menuText("Image", QStringLiteral("图片")));
    ui_.ui->officeTypeBtn->setText(menuText("Office", QStringLiteral("Office")));
    ui_.ui->richTextTypeBtn->setText(menuText("Rich Text", QStringLiteral("富文本")));
    ui_.ui->fileTypeBtn->setText(menuText("File", QStringLiteral("文件")));

    ui_.ui->searchButton->setText(QString());
    ui_.ui->searchButton->setIcon(IconResolver::themedIcon(QStringLiteral("search"), ThemeManager::instance()->isDark()));
    ui_.ui->searchButton->setIconSize(QSize(16, 16));
    ui_.ui->searchButton->setToolButtonStyle(Qt::ToolButtonIconOnly);

    ui_.ui->menuButton->setText(QString());
    ui_.ui->menuButton->setIcon(IconResolver::themedIcon(QStringLiteral("settings"), ThemeManager::instance()->isDark()));
    ui_.ui->menuButton->setIconSize(QSize(20, 20));
    ui_.ui->menuButton->setFixedSize(QSize(32, 28));
    ui_.ui->menuButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    ui_.ui->menuButton->setToolTip(menuText("More", QStringLiteral("更多")));

    ui_.pageSelectorWidget = new PageSelectorPill(this);
    auto *pageLayout = new QHBoxLayout(ui_.pageSelectorWidget);
    pageLayout->setContentsMargins(8, 0, 8, 0);
    pageLayout->setSpacing(3);

    ui_.pagePrefixLabel = new QLabel(QStringLiteral("第"), ui_.pageSelectorWidget);
    ui_.pagePrefixLabel->setAlignment(Qt::AlignCenter);
    ui_.pagePrefixLabel->setFixedWidth(14);

    // Hidden QComboBox kept for compatibility with updatePageSelector() which
    // reads its count/index.  The visible control is pageNumberLabel.
    ui_.pageComboBox = new QComboBox(ui_.pageSelectorWidget);
    ui_.pageComboBox->hide();
    ui_.pageComboBox->addItem(QStringLiteral("1"), 1);
    ui_.pageComboBox->setCurrentIndex(0);

    ui_.pageNumberLabel = new QLabel(QStringLiteral("1"), ui_.pageSelectorWidget);
    ui_.pageNumberLabel->setAlignment(Qt::AlignCenter);
    ui_.pageNumberLabel->setCursor(Qt::PointingHandCursor);
    ui_.pageNumberLabel->setFixedWidth(28);
    ui_.pageNumberLabel->installEventFilter(this);

    ui_.pageTotalLabel = new QLabel(QStringLiteral("/ 1"), ui_.pageSelectorWidget);
    ui_.pageTotalLabel->setAlignment(Qt::AlignCenter);
    ui_.pageTotalLabel->setFixedWidth(36);

    ui_.pageSuffixLabel = new QLabel(QStringLiteral("页"), ui_.pageSelectorWidget);
    ui_.pageSuffixLabel->setAlignment(Qt::AlignCenter);
    ui_.pageSuffixLabel->setFixedWidth(14);

    pageLayout->addWidget(ui_.pagePrefixLabel);
    pageLayout->addWidget(ui_.pageNumberLabel);
    pageLayout->addWidget(ui_.pageTotalLabel);
    pageLayout->addWidget(ui_.pageSuffixLabel);
    ui_.pageSelectorWidget->setFixedWidth(110);

    if (ui_.ui->horizontalLayout) {
        const int countIndex = ui_.ui->horizontalLayout->indexOf(ui_.ui->countArea);
        ui_.ui->horizontalLayout->insertWidget(countIndex >= 0 ? countIndex : ui_.ui->horizontalLayout->count(),
                                               ui_.pageSelectorWidget);
    }
    ui_.ui->countArea->setFixedWidth(72);

    applyScale(MPasteSettings::getInst()->getItemScale());

    ui_.buttonGroup = new QButtonGroup(this);
    ui_.buttonGroup->setExclusive(true);
    ui_.buttonGroup->addButton(ui_.ui->clipboardButton);
    ui_.buttonGroup->addButton(ui_.ui->staredButton);

    ui_.typeButtonGroup = new QButtonGroup(this);
    ui_.typeButtonGroup->setExclusive(true);

    ui_.ui->allTypeBtn->setProperty("contentType", static_cast<int>(All));
    ui_.ui->textTypeBtn->setProperty("contentType", static_cast<int>(Text));
    ui_.ui->linkTypeBtn->setProperty("contentType", static_cast<int>(Link));
    ui_.ui->imageTypeBtn->setProperty("contentType", static_cast<int>(Image));
    ui_.ui->officeTypeBtn->setProperty("contentType", static_cast<int>(Office));
    ui_.ui->richTextTypeBtn->setProperty("contentType", static_cast<int>(RichText));
    ui_.ui->fileTypeBtn->setProperty("contentType", static_cast<int>(File));

    ui_.typeButtonGroup->addButton(ui_.ui->allTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->textTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->linkTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->imageTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->officeTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->richTextTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->fileTypeBtn);

    ui_.ui->allTypeBtn->setChecked(true);

    ui_.ui->clipboardButton->setChecked(true);

    initMenu();

    ui_.ui->searchEdit->installEventFilter(this);
    ui_.ui->clipboardBtnWidget->installEventFilter(this);
    ui_.ui->typeBtnWidget->installEventFilter(this);

    applyTheme(ThemeManager::instance()->isDark());
    updatePageSelector();
    updatePageSelectorStyle();
}

AboutWidget *MPasteWidget::ensureAboutWidget() {
    if (!ui_.aboutWidget) {
        ui_.aboutWidget = new AboutWidget(this);
        ui_.aboutWidget->hide();
    }
    return ui_.aboutWidget;
}

ClipboardItemDetailsDialog *MPasteWidget::ensureDetailsDialog() {
    if (!ui_.detailsDialog) {
        ui_.detailsDialog = new ClipboardItemDetailsDialog(this);
        ui_.detailsDialog->setWindowFlag(Qt::Tool);
        ui_.detailsDialog->hide();
    }
    return ui_.detailsDialog;
}

ClipboardItemPreviewDialog *MPasteWidget::ensurePreviewDialog() {
    if (!ui_.previewDialog) {
        ui_.previewDialog = new ClipboardItemPreviewDialog(this);
        ui_.previewDialog->setWindowFlag(Qt::Tool);
        ui_.previewDialog->hide();
    }
    return ui_.previewDialog;
}

MPasteSettingsWidget *MPasteWidget::ensureSettingsWidget() {
    if (!ui_.settingsWidget) {
        ui_.settingsWidget = new MPasteSettingsWidget(this);
        connect(ui_.settingsWidget, &MPasteSettingsWidget::shortcutChanged,
                this, &MPasteWidget::shortcutChanged);
        connect(ui_.settingsWidget, &MPasteSettingsWidget::historyRetentionChanged,
                this, &MPasteWidget::reloadHistoryBoards);
        connect(ui_.settingsWidget, &MPasteSettingsWidget::historyViewModeChanged,
                this, &MPasteWidget::reloadHistoryBoards);
        connect(ui_.settingsWidget, &MPasteSettingsWidget::saveDirChanged,
                this, [this]() {
                    setupSyncWatcher();
                    reloadHistoryBoards();
                });
        connect(ui_.settingsWidget, &MPasteSettingsWidget::itemScaleChanged,
                this, [this](int scale) {
                    applyScale(scale);
                });
    }
    return ui_.settingsWidget;
}

void MPasteWidget::scheduleStartupWarmup() {
    if (loading_.startupWarmupScheduled) {
        return;
    }
    loading_.startupWarmupScheduled = true;

    QTimer::singleShot(0, this, [this]() {
        loadFromSaveDir();
        qInfo().noquote() << QStringLiteral("[startup] deferred loadFromSaveDir done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());

        // Wait for the clipboard board to finish loading before priming
        // the clipboard, so duplicate detection has the full history.
        // Always wait — hasPendingItems() is unreliable during async scan.
        auto *boardService = ui_.clipboardWidget->boardServiceRef();
        if (boardService) {
            connect(boardService, &ClipboardBoardService::deferredLoadCompleted, this, [this]() {
                clipboard_.monitor->primeCurrentClipboard();
                qInfo().noquote() << QStringLiteral("[startup] deferred primeCurrentClipboard done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
                loading_.startupWarmupCompleted = true;
            }, Qt::SingleShotConnection);
        }
    });
}

void MPasteWidget::applyScale(int scale) {
    if (ui_.clipboardWidget) {
        ui_.clipboardWidget->applyScale(scale);
    }
    if (ui_.staredWidget) {
        ui_.staredWidget->applyScale(scale);
    }

    auto scaleFont = [scale](QWidget *widget) {
        if (!widget) {
            return;
        }
        QFont font = widget->font();
        QVariant baseVar = widget->property("baseFontPt");
        qreal base = baseVar.isValid() ? baseVar.toReal() : font.pointSizeF();
        if (!baseVar.isValid()) {
            widget->setProperty("baseFontPt", base);
        }
        if (base > 0) {
            font.setPointSizeF(qMax<qreal>(8.0, base * scale / 100.0));
            widget->setFont(font);
        }
    };

    auto scaleHeight = [scale](QWidget *widget, int minBase = 24) {
        if (!widget) {
            return;
        }
        QVariant baseVar = widget->property("baseHeight");
        int base = baseVar.isValid() ? baseVar.toInt() : widget->sizeHint().height();
        if (!baseVar.isValid()) {
            widget->setProperty("baseHeight", base);
        }
        const int height = qMax(minBase, base * scale / 100);
        widget->setMinimumHeight(height);
        widget->setMaximumHeight(height);
    };
    auto applyRadius = [](QWidget *widget) {
        if (!widget) {
            return;
        }
        const int height = qMax(1, widget->maximumHeight());
        const int radius = qMax(6, height / 2);
        widget->setStyleSheet(QStringLiteral("border-radius: %1px;").arg(radius));
    };

    const int topBarHeight = qMax(32, 36 * scale / 100);
    if (ui_.ui->topLine) {
        ui_.ui->topLine->setFixedHeight(qMax(1, scale / 20));
    }
    scaleHeight(ui_.ui->widget, topBarHeight);

    const int tabButtonHeight = qMax(24, 28 * scale / 100);
    scaleHeight(ui_.ui->clipboardButton, tabButtonHeight);
    scaleHeight(ui_.ui->staredButton, tabButtonHeight);
    scaleHeight(ui_.ui->allTypeBtn, tabButtonHeight);
    scaleHeight(ui_.ui->textTypeBtn, tabButtonHeight);
    scaleHeight(ui_.ui->linkTypeBtn, tabButtonHeight);
    scaleHeight(ui_.ui->imageTypeBtn, tabButtonHeight);
    scaleHeight(ui_.ui->officeTypeBtn, tabButtonHeight);
    scaleHeight(ui_.ui->richTextTypeBtn, tabButtonHeight);
    scaleHeight(ui_.ui->fileTypeBtn, tabButtonHeight);
    scaleFont(ui_.ui->clipboardButton);
    scaleFont(ui_.ui->staredButton);
    scaleFont(ui_.ui->allTypeBtn);
    scaleFont(ui_.ui->textTypeBtn);
    scaleFont(ui_.ui->linkTypeBtn);
    scaleFont(ui_.ui->imageTypeBtn);
    scaleFont(ui_.ui->officeTypeBtn);
    scaleFont(ui_.ui->richTextTypeBtn);
    scaleFont(ui_.ui->fileTypeBtn);

    applyRadius(ui_.ui->clipboardButton);
    applyRadius(ui_.ui->staredButton);
    applyRadius(ui_.ui->allTypeBtn);
    applyRadius(ui_.ui->textTypeBtn);
    applyRadius(ui_.ui->linkTypeBtn);
    applyRadius(ui_.ui->imageTypeBtn);
    applyRadius(ui_.ui->officeTypeBtn);
    applyRadius(ui_.ui->richTextTypeBtn);
    applyRadius(ui_.ui->fileTypeBtn);

    if (ui_.ui->searchEdit) {
        scaleHeight(ui_.ui->searchEdit, tabButtonHeight);
    }
    if (ui_.ui->firstButton) {
        scaleHeight(ui_.ui->firstButton, tabButtonHeight);
    }
    if (ui_.ui->lastButton) {
        scaleHeight(ui_.ui->lastButton, tabButtonHeight);
    }
    if (ui_.pageComboBox) {
        scaleHeight(ui_.pageComboBox, tabButtonHeight);
        scaleFont(ui_.pageComboBox);
    }
    if (ui_.pagePrefixLabel) {
        scaleFont(ui_.pagePrefixLabel);
        ui_.pagePrefixLabel->setFixedWidth(qMax(12, 14 * scale / 100));
    }
    if (ui_.pageTotalLabel) {
        scaleFont(ui_.pageTotalLabel);
        ui_.pageTotalLabel->setFixedWidth(qMax(32, 36 * scale / 100));
    }
    if (ui_.pageSuffixLabel) {
        scaleFont(ui_.pageSuffixLabel);
        ui_.pageSuffixLabel->setFixedWidth(qMax(12, 14 * scale / 100));
    }
    if (ui_.pageSelectorWidget) {
        ui_.pageSelectorWidget->setFixedHeight(tabButtonHeight);
        ui_.pageSelectorWidget->setFixedWidth(qMax(96, 110 * scale / 100));
    }
    if (ui_.ui->countArea) {
        ui_.ui->countArea->setFixedWidth(qMax(64, 72 * scale / 100));
    }
    updatePageSelector();
    updatePageSelectorStyle();

    if (ui_.layout) {
        ui_.layout->setContentsMargins(0, qMax(6, 10 * scale / 100), 0, 0);
    }

    const int newHeight = ui_.ui->widget->height()
        + ui_.ui->itemsWidget->sizeHint().height()
        + qMax(6, 10 * scale / 100)
        + qMax(1, scale / 20);
    setFixedHeight(newHeight);

    QScreen *screen = this->screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QRect avail = screen->availableGeometry();
        QRect geom = geometry();
        const int newWidth = geom.width();
        int newY = avail.bottom() - newHeight + 1;
        if (newY < avail.top()) {
            newY = avail.top();
        }
        int newX = geom.x();
        if (newX + newWidth > avail.right()) {
            newX = avail.right() - newWidth + 1;
        }
        if (newX < avail.left()) {
            newX = avail.left();
        }
        setGeometry(newX, newY, newWidth, newHeight);
    }
}

void MPasteWidget::initSearchAnimations() {
    ui_.searchHideAnim = new QPropertyAnimation(ui_.ui->searchEdit, "maximumWidth");
    ui_.searchHideAnim->setEndValue(0);
    ui_.searchHideAnim->setDuration(150);
    connect(ui_.searchHideAnim, &QPropertyAnimation::finished, ui_.ui->searchEdit, &QLineEdit::hide);

    ui_.searchShowAnim = new QPropertyAnimation(ui_.ui->searchEdit, "maximumWidth");
    ui_.searchShowAnim->setEndValue(200);
    ui_.searchShowAnim->setDuration(150);
}

void MPasteWidget::initClipboard() {
    clipboard_.monitor = new ClipboardMonitor();
    clipboard_.copiedWhenHide = false;

    pasteController_ = new ClipboardPasteController(clipboard_.monitor, this);
    connect(pasteController_, &ClipboardPasteController::pastingFinished, this, [this]() {
        // pastingFinished is emitted after the isPasting flag is cleared internally
    });
}

void MPasteWidget::initShortcuts() {
    misc_.numKeyList.clear();
    misc_.numKeyList << Qt::Key_1 << Qt::Key_2 << Qt::Key_3 << Qt::Key_4 << Qt::Key_5
                     << Qt::Key_6 << Qt::Key_7 << Qt::Key_8 << Qt::Key_9 << Qt::Key_0;
}

void MPasteWidget::initSound() {
    copySoundPlayer_ = new CopySoundPlayer(
        MPasteSettings::getInst()->isPlaySound(),
        QUrl(QStringLiteral("qrc:/resources/resources/sound.mp3")),
        this);
}

void MPasteWidget::initSystemTray() {
    ui_.trayIcon = new QSystemTrayIcon(this);
    ui_.trayIcon->setIcon(QIcon(":/resources/resources/mpaste.svg"));
    ui_.trayIcon->setContextMenu(ui_.trayMenu ? ui_.trayMenu : ui_.menu);
    ui_.trayIcon->show();
}

void MPasteWidget::initMenu() {
    ui_.menu = new QMenu(this);
    ui_.trayMenu = new QMenu(this);

    ui_.aboutAction = new QAction(
        IconResolver::themedIcon(QStringLiteral("info"), ThemeManager::instance()->isDark()),
        menuText("About", QString::fromUtf16(u"\u5173\u4E8E")),
        this);
    connect(ui_.aboutAction, &QAction::triggered, this, [this]() {
        AboutWidget *aboutWidget = ensureAboutWidget();
        QScreen *screen = QGuiApplication::primaryScreen();
        if (const QWindow *window = windowHandle())
            screen = window->screen();
        if (!screen)
            return;

        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - aboutWidget->width()) / 2;
        int y = (screenGeometry.height() - aboutWidget->height()) / 2;
        aboutWidget->move(screenGeometry.x() + x, screenGeometry.y() + y);
        aboutWidget->show();
        aboutWidget->applyTheme(darkTheme_);
        aboutWidget->raise();
        aboutWidget->activateWindow();
    });

    ui_.settingsAction = new QAction(
        IconResolver::themedIcon(QStringLiteral("settings"), ThemeManager::instance()->isDark()),
        menuText("Settings", QString::fromUtf16(u"\u8BBE\u7F6E")),
        this);
    connect(ui_.settingsAction, &QAction::triggered, this, [this]() {
        MPasteSettingsWidget *settingsWidget = ensureSettingsWidget();
        QScreen *screen = QGuiApplication::primaryScreen();
        if (const QWindow *window = windowHandle())
            screen = window->screen();
        if (!screen)
            return;

        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - settingsWidget->width()) / 2;
        int y = (screenGeometry.height() - settingsWidget->height()) / 2;
        settingsWidget->move(screenGeometry.x() + x, screenGeometry.y() + y);
        settingsWidget->show();
    });

    ui_.quitAction = new QAction(
        QIcon(QStringLiteral(":/resources/resources/quit.svg")),
        menuText("Quit", QString::fromUtf16(u"\u9000\u51FA")),
        this);
    connect(ui_.quitAction, &QAction::triggered, this, []() { qApp->exit(0); });

    const auto addActions = [this](QMenu *menu) {
        if (!menu) {
            return;
        }
        menu->addAction(ui_.aboutAction);
        menu->addAction(ui_.settingsAction);
        menu->addSeparator();
        menu->addAction(ui_.quitAction);
    };
    addActions(ui_.menu);
    addActions(ui_.trayMenu);

    BoardHelpers::applyMenuTheme(ui_.menu);
    BoardHelpers::applyMenuTheme(ui_.trayMenu);
}


void MPasteWidget::applyTheme(bool dark) {
    darkTheme_ = dark;

    WindowBlurHelper::enableBlurBehind(this, darkTheme_);

    if (ui_.ui) {
        ui_.ui->menuButton->setIcon(IconResolver::themedIcon(QStringLiteral("settings"), darkTheme_));
        ui_.ui->searchButton->setIcon(QIcon(darkTheme_
            ? QStringLiteral(":/resources/resources/search_light.svg")
            : QStringLiteral(":/resources/resources/search.svg")));
        ui_.ui->searchButton->setIconSize(QSize(16, 16));
        ui_.ui->searchButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        ui_.ui->searchButton->setText(QString());
        ui_.ui->firstButton->setIcon(IconResolver::themedIcon(QStringLiteral("first"), darkTheme_));
        ui_.ui->lastButton->setIcon(IconResolver::themedIcon(QStringLiteral("last"), darkTheme_));
    }

    if (ui_.aboutAction) {
        ui_.aboutAction->setIcon(IconResolver::themedIcon(QStringLiteral("info"), darkTheme_));
    }
    if (ui_.settingsAction) {
        ui_.settingsAction->setIcon(IconResolver::themedIcon(QStringLiteral("settings"), darkTheme_));
    }
    if (ui_.quitAction) {
        ui_.quitAction->setIcon(QIcon(QStringLiteral(":/resources/resources/quit.svg")));
    }
    // Force glass-style tab borders in dark mode (QSS tokens don't override reliably)
    if (darkTheme_ && ui_.ui) {
        const QString glassTabStyle = QStringLiteral(
            "QToolButton { background-color: transparent; border: none; border-radius: 12px; }"
            "QToolButton:hover { background-color: rgba(255,255,255,22); }"
            "QToolButton:checked, QToolButton:pressed { background-color: rgba(255,255,255,50); }");
        ui_.ui->clipboardBtnWidget->setStyleSheet(glassTabStyle);
        ui_.ui->typeBtnWidget->setStyleSheet(glassTabStyle);
    } else if (ui_.ui) {
        // Light mode: glass style with dark tint
        const QString lightGlassTabStyle = QStringLiteral(
            "QToolButton { background-color: transparent; border: none; border-radius: 12px; }"
            "QToolButton:hover { background-color: rgba(0,0,0,12); }"
            "QToolButton:checked, QToolButton:pressed { background-color: rgba(0,0,0,22); }");
        ui_.ui->clipboardBtnWidget->setStyleSheet(lightGlassTabStyle);
        ui_.ui->typeBtnWidget->setStyleSheet(lightGlassTabStyle);
    }

    updatePageSelectorStyle();
    BoardHelpers::applyMenuTheme(ui_.menu);
    BoardHelpers::applyMenuTheme(ui_.trayMenu);
    update();
}

void MPasteWidget::setupConnections() {
    connect(clipboard_.monitor, &ClipboardMonitor::clipboardActivityObserved,
            this, &MPasteWidget::clipboardActivityObserved);
    connect(clipboard_.monitor, &ClipboardMonitor::clipboardUpdated,
            this, &MPasteWidget::clipboardUpdated);
    connect(clipboard_.monitor, &ClipboardMonitor::clipboardMimeCompleted,
            this, [this](const QString &itemName, const QMap<QString, QByteArray> &extraFormats) {
        ui_.clipboardWidget->mergeDeferredMimeFormats(itemName, extraFormats);
    });
    for (auto *boardWidget : ui_.boardWidgetMap.values()) {
        connect(boardWidget, &ScrollItemsWidget::doubleClicked,
        this, [this](const ClipboardItem &item) {
            if (pasteController_->setClipboard(item)) {
                this->hideAndPaste();
            }
        });

        connect(boardWidget, &ScrollItemsWidget::plainTextPasteRequested,
        this, [this](const ClipboardItem &item) {
            if (pasteController_->setClipboard(item, true)) {
                this->hideAndPaste();
            }
        });

        connect(boardWidget, &ScrollItemsWidget::detailsRequested,
        this, [this](const ClipboardItem &item, int sequence, int totalCount) {
            ensureDetailsDialog()->showItem(item, sequence, totalCount);
        });
        connect(boardWidget, &ScrollItemsWidget::previewRequested,
        this, [this](const ClipboardItem &item) {
            if (ClipboardItemPreviewDialog::supportsPreview(item)) {
                ensurePreviewDialog()->showItem(item);
            }
        });

        connect(boardWidget, &ScrollItemsWidget::ocrRequested,
        this, [this, boardWidget](const ClipboardItem &item) {
            ensureOcrService();
            // Check if OCR result already exists in sidecar cache.
            {
                auto *bs = boardWidget->boardServiceRef();
                if (bs) {
                    const QString fp = bs->filePathForName(item.getName());
                    const OcrService::Result cached = OcrService::readSidecar(fp);
                    if (cached.status == OcrService::Ready && !cached.text.isEmpty()) {
                        showOcrResultDialog(cached.text);
                        return;
                    }
                }
            }
            // Load the full item from disk first (loadFromFile sets
            // mimeDataLoaded_=true so getImage() returns the real
            // payload, not just the icon/thumbnail).  Fall back to
            // the in-memory item for freshly captured entries that
            // haven't been saved yet.
            QImage image;
            auto *bs = boardWidget->boardServiceRef();
            if (bs) {
                const QString fp = bs->filePathForName(item.getName());
                if (QFile::exists(fp)) {
                    LocalSaver saver;
                    ClipboardItem diskItem = saver.loadFromFile(fp);
                    image = diskItem.getImage().toImage();
                }
            }
            if (image.isNull() || (image.width() <= 64 && image.height() <= 64)) {
                ClipboardItem memItem(item);
                memItem.ensureMimeDataLoaded();
                QImage memImage = memItem.getImage().toImage();
                if (!memImage.isNull() && memImage.width() > image.width()) {
                    image = memImage;
                }
            }
            if (image.isNull()) {
                image = item.thumbnail().toImage();
            }
            if (image.isNull()) {
                QMessageBox::warning(this, tr("OCR"), tr("No image data available for OCR."));
                return;
            }
            // Show loading dialog.
            if (ocrLoadingDialog_) {
                ocrLoadingDialog_->close();
            }
            {
                const bool dark = MPasteSettings::getInst()->isDarkTheme();
                auto *dlg = new OcrResultDialog(this);
                dlg->dark = dark;
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->setAttribute(Qt::WA_TranslucentBackground);
                dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
                dlg->setModal(false);
                dlg->resize(320, 120);

                auto *lay = new QVBoxLayout(dlg);
                lay->setAlignment(Qt::AlignCenter);
                auto *label = new QLabel(tr("Recognizing..."), dlg);
                label->setAlignment(Qt::AlignCenter);
                const QString color = dark ? QStringLiteral("#E6EDF5") : QStringLiteral("#1E2936");
                label->setStyleSheet(QStringLiteral("color: %1; font-size: 18px; background: transparent;").arg(color));
                lay->addWidget(label);

                QScreen *screen = nullptr;
                if (auto *w = window()->windowHandle()) screen = w->screen();
                if (!screen) screen = QGuiApplication::screenAt(QCursor::pos());
                if (!screen) screen = QGuiApplication::primaryScreen();
                if (screen) {
                    const QRect geo = screen->availableGeometry();
                    dlg->move(geo.center() - QPoint(dlg->width() / 2, dlg->height() / 2));
                }
                dlg->show();
                WindowBlurHelper::enableBlurBehind(dlg, dark);
                dlg->raise();
                ocrLoadingDialog_ = dlg;
            }
            manualOcrItems_.insert(item.getName());
            ocrService_->requestOcr(item.getName(), image);
        });

        connect(boardWidget, &ScrollItemsWidget::itemCountChanged, this, [this, boardWidget](int itemCount) {
            if (ui_.buttonGroup->checkedButton()->property("category").toString() == boardWidget->getCategory()) {
                this->updateItemCount(itemCount);
            }
        });
        connect(boardWidget, &ScrollItemsWidget::selectionStateChanged, this, [this, boardWidget]() {
            if (ui_.buttonGroup->checkedButton()->property("category").toString() == boardWidget->getCategory()) {
                this->updateItemCount(boardWidget->getItemCount());
            }
        });
        connect(boardWidget, &ScrollItemsWidget::pageStateChanged, this, [this, boardWidget](int, int) {
            if (ui_.buttonGroup->checkedButton()->property("category").toString() == boardWidget->getCategory()) {
                this->updatePageSelector();
            }
        });

        connect(boardWidget, &ScrollItemsWidget::itemStared, this, [this](const ClipboardItem &item) {
            ClipboardItem updatedItem(item);
            if (!hasUsableMimeData(updatedItem)) {
                ClipboardItem rehydrated = rehydrateClipboardItem(updatedItem);
                if (!rehydrated.getName().isEmpty()) {
                    updatedItem = rehydrated;
                }
            }
            ui_.staredWidget->addAndSaveItem(updatedItem);
            ui_.clipboardWidget->setItemFavorite(updatedItem, true);
        });
        connect(boardWidget, &ScrollItemsWidget::itemUnstared, this, [this, boardWidget](const ClipboardItem &item) {
            ui_.staredWidget->removeItemByContent(item);
            // Sync: un-star in other boards too
            if (boardWidget == ui_.staredWidget) {
                ui_.clipboardWidget->setItemFavorite(item, false);
            }
        });
        connect(boardWidget, &ScrollItemsWidget::aliasChanged, this, [this, boardWidget](const QByteArray &fingerprint, const QString &alias) {
            for (auto *other : ui_.boardWidgetMap.values()) {
                if (other && other != boardWidget) {
                    other->syncAlias(fingerprint, alias);
                }
            }
        });
        connect(boardWidget, &ScrollItemsWidget::localPersistenceChanged, this, [this]() {
            if (syncWatcher_) {
                syncWatcher_->suppressReloadUntil(QDateTime::currentMSecsSinceEpoch() + 800);
            }
        });
    }

    connect(ui_.ui->menuButton, &QToolButton::clicked, this, [this]() {
        ui_.menu->popup(ui_.ui->menuButton->mapToGlobal(ui_.ui->menuButton->rect().bottomLeft()));
    });

    connect(ui_.ui->searchEdit, &QLineEdit::textChanged, this, [this](const QString &str) {
        this->currItemsWidget()->filterByKeyword(str);
    });
    connect(ui_.ui->searchButton, &QToolButton::clicked, this, [this](bool flag) {
        this->setFocusOnSearch(flag);
    });
    connect(ui_.pageComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) {
            return;
        }
        if (ScrollItemsWidget *board = currItemsWidget()) {
            board->setCurrentPageNumber(index + 1);
        }
        if (ui_.pageNumberLabel) {
            ui_.pageNumberLabel->setText(QString::number(index + 1));
        }
    });

    connect(ui_.ui->firstButton, &QPushButton::clicked, this, [this]() {
        this->currItemsWidget()->scrollToFirst();
    });
    connect(ui_.ui->lastButton, &QPushButton::clicked, this, [this]() {
        this->currItemsWidget()->scrollToLast();
    });

    connect(ui_.trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            this->setVisibleWithAnnimation(true);
        }
    });

    connect(ui_.typeButtonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
        [this](QAbstractButton *button) {
            auto type = static_cast<ContentType>(button->property("contentType").toInt());
            this->currItemsWidget()->filterByType(type);
        });

    connect(ui_.buttonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
        [this](QAbstractButton *button) {
            auto typeBtn = ui_.typeButtonGroup->checkedButton();
            auto type = typeBtn ? static_cast<ContentType>(typeBtn->property("contentType").toInt())
                                : All;

            for (auto *toolButton : ui_.buttonGroup->buttons()) {
                auto *boardWidget = ui_.boardWidgetMap[toolButton->property("category").toString()];
                boardWidget->setVisible(toolButton == button);
                if (toolButton == button) {
                    boardWidget->filterByType(type);
                    boardWidget->filterByKeyword(ui_.ui->searchEdit->text());
                }
            }
            updateItemCount(currItemsWidget()->getItemCount());
            updatePageSelector();
        });
}

// Sound playback is now delegated to copySoundPlayer_.

void MPasteWidget::clipboardActivityObserved(int wId) {
    if (pasteController_->isPasting()) {
        return;
    }
    copySoundPlayer_->playCopySoundIfNeeded(wId);
}

void MPasteWidget::clipboardUpdated(const ClipboardItem &nItem, int wId) {
    if (pasteController_->isPasting()) {
        return;
    }

    // Suppress echo from our own clipboard write: if the captured item
    // has the same fingerprint as the item we just pasted, skip the add
    // to avoid creating a duplicate that pushes the original down.
    const QByteArray pastedFp = pasteController_->lastPastedFingerprint();
    if (!pastedFp.isEmpty() && nItem.fingerprint() == pastedFp) {
        pasteController_->clearLastPastedFingerprint();
        qInfo().noquote() << QStringLiteral("[clipboard-widget] clipboardUpdated suppressed echo wId=%1 fp=%2")
            .arg(wId)
            .arg(QString::fromLatin1(pastedFp.toHex().left(12)));
        return;
    }

    const bool added = ui_.clipboardWidget->addAndSaveItem(nItem);
    qInfo().noquote() << QStringLiteral("[clipboard-widget] clipboardUpdated wId=%1 isPasting=%2 added=%3 %4")
        .arg(wId)
        .arg(pasteController_->isPasting())
        .arg(added)
        .arg(widgetItemSummary(nItem));

    if (added) {
        clipboard_.copiedWhenHide = true;

        // Auto OCR for image items when enabled in settings.
        const ContentType ct = nItem.getContentType();
        if ((ct == Image || ct == Office)
            && MPasteSettings::getInst()->isAutoOcr()) {
            // Check if sidecar already exists (avoid re-OCR).
            auto *bs = ui_.clipboardWidget->boardServiceRef();
            const QString fp = bs ? bs->filePathForName(nItem.getName()) : QString();
            if (!fp.isEmpty() && OcrService::readSidecar(fp).status == OcrService::None) {
                ensureOcrService();
                // Get image for OCR.
                QImage image;
                {
                    ClipboardItem memItem(nItem);
                    memItem.ensureMimeDataLoaded();
                    image = memItem.getImage().toImage();
                }
                if (image.isNull() || (image.width() <= 64 && image.height() <= 64)) {
                    if (QFile::exists(fp)) {
                        LocalSaver saver;
                        ClipboardItem diskItem = saver.loadFromFile(fp);
                        image = diskItem.getImage().toImage();
                    }
                }
                if (!image.isNull()) {
                    qInfo() << "[ocr] auto-ocr for" << nItem.getName();
                    if (auto *d = ui_.clipboardWidget->cardDelegateRef()) {
                        d->markOcrPending(nItem.getName());
                    }
                    ocrService_->requestOcr(nItem.getName(), image);
                }
            }
        }
    }
}

// Clipboard write and URL handling are now delegated to pasteController_.
void MPasteWidget::handleKeyboardEvent(QKeyEvent *event) {
    switch (event->key()) {
        case Qt::Key_Escape:
            handleEscapeKey();
            break;
        case Qt::Key_Alt:
            currItemsWidget()->setShortcutInfo();
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            handleEnterKey(event->modifiers().testFlag(Qt::ControlModifier));
            break;
        case Qt::Key_Space:
            handlePreviewKey();
            break;
        case Qt::Key_Left:
        case Qt::Key_Right:
            handleNavigationKeys(event);
            break;
        case Qt::Key_Home:
        case Qt::Key_End:
            handleHomeEndKeys(event);
            break;
        case Qt::Key_Tab:
            handleTabKey();
            break;
        case Qt::Key_M:
            if (event->modifiers().testFlag(Qt::ControlModifier)) {
                dumpMemoryStats();
                return;
            }
            handleSearchInput(event);
            break;
        default:
            handleSearchInput(event);
            break;
    }
}

void MPasteWidget::handleTabKey() {
    QAbstractButton* currentButton = ui_.buttonGroup->checkedButton();
    QList<QAbstractButton*> buttons = ui_.buttonGroup->buttons();
    int currentIndex = buttons.indexOf(currentButton);
    int nextIndex = (currentIndex + 1) % buttons.size();
    buttons[nextIndex]->click();
}

void MPasteWidget::handleEscapeKey() {
    if (ui_.ui->searchEdit->hasFocus() || !ui_.ui->searchEdit->text().isEmpty()) {
        ui_.ui->searchEdit->clear();
        setFocusOnSearch(false);
    } else {
        hide();
    }
}

void MPasteWidget::handleEnterKey(bool plainText) {
    if (currItemsWidget()->hasMultipleSelectedItems()) {
        return;
    }

    auto *board = currItemsWidget();
    const ClipboardItem *selectedItem = board->selectedByEnter();
    if (selectedItem && pasteController_->setClipboard(*selectedItem, plainText)) {
        hideAndPaste();
        board->moveSelectedToFirst();
    }
}

void MPasteWidget::handlePreviewKey() {
    ClipboardItemPreviewDialog *previewDialog = ensurePreviewDialog();
    if (previewDialog->isVisible()) {
        previewDialog->reject();
        return;
    }

    if (currItemsWidget()->hasMultipleSelectedItems()) {
        return;
    }

    const ClipboardItem *selectedItem = currItemsWidget()->currentSelectedItem();
    if (!selectedItem || !ClipboardItemPreviewDialog::supportsPreview(*selectedItem)) {
        return;
    }

    previewDialog->showItem(*selectedItem);
}

bool MPasteWidget::triggerShortcutPaste(int shortcutIndex, bool plainText) {
    if (shortcutIndex < 0 || shortcutIndex > 9) {
        return false;
    }

    auto *board = currItemsWidget();
    const ClipboardItem *selectedItem = board->selectedByShortcut(shortcutIndex);
    if (!selectedItem || !pasteController_->setClipboard(*selectedItem, plainText)) {
        return false;
    }

    const QString itemName = selectedItem->getName();
    qInfo().noquote() << QStringLiteral("[shortcut-paste] index=%1 itemName=%2")
        .arg(shortcutIndex).arg(itemName);
    QTimer::singleShot(50, this, [this, board, itemName]() {
        hideAndPaste();
        board->moveItemByNameToFirst(itemName);
        currItemsWidget()->cleanShortCutInfo();
    });
    return true;
}

void MPasteWidget::handleNavigationKeys(QKeyEvent *event) {
    if (!ui_.ui->searchEdit->isVisible()) {
        if (event->key() == Qt::Key_Left) {
            currItemsWidget()->focusMoveLeft();
        } else {
            currItemsWidget()->focusMoveRight();
        }
    } else if (ui_.ui->searchEdit->isVisible()) {
        QGuiApplication::sendEvent(ui_.ui->searchEdit, event);
        setFocusOnSearch(true);
    }
}

void MPasteWidget::handleHomeEndKeys(QKeyEvent *event) {
    if (!ui_.ui->searchEdit->isVisible()) {
        if (event->key() == Qt::Key_Home) {
            currItemsWidget()->scrollToFirst();
        } else {
            currItemsWidget()->scrollToLast();
        }
    }
}

void MPasteWidget::handleSearchInput(QKeyEvent *event) {
    if (event->key() < Qt::Key_Space || event->key() > Qt::Key_AsciiTilde) {
        return;
    }

    Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers & (Qt::AltModifier | Qt::ControlModifier)) {
        event->ignore();
        return;
    }

    if (!ui_.ui->searchEdit->hasFocus()) {
        ui_.ui->searchEdit->setFocus();
        setFocusOnSearch(true);
    }

    QString currentText = ui_.ui->searchEdit->text();
    currentText += event->text();
    ui_.ui->searchEdit->setText(currentText);
    event->accept();
}

bool MPasteWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Paint &&
        (watched == ui_.ui->clipboardBtnWidget || watched == ui_.ui->typeBtnWidget)) {
        QWidget *w = qobject_cast<QWidget*>(watched);
        QPainter p(w);
        p.setRenderHint(QPainter::Antialiasing);

        const qreal radius = 13.0;
        QRectF r = QRectF(w->rect());

        if (darkTheme_) {
            // Glass pill container
            r = r.adjusted(0.5, 0.5, -0.5, -0.5);
            p.setPen(QPen(QColor(255, 255, 255, 30), 1.0));
            p.setBrush(QColor(255, 255, 255, 10));
            p.drawRoundedRect(r, radius, radius);
        } else {
            // Light: glass pill container with dark tint
            r = r.adjusted(0.5, 0.5, -0.5, -0.5);
            p.setPen(QPen(QColor(0, 0, 0, 25), 1.0));
            p.setBrush(QColor(0, 0, 0, 8));
            p.drawRoundedRect(r, radius, radius);
        }
    }

    if (event->type() == QEvent::Wheel) {
#if (QT_VERSION >= QT_VERSION_CHECK(5,12,0))
        return this->currItemsWidget()->handleWheelScroll(static_cast<QWheelEvent *>(event));
#endif
    } else if (event->type() == QEvent::KeyPress) {
        if (watched == ui_.ui->searchEdit) {
            auto keyEvent = dynamic_cast<QKeyEvent*>(event);
            if (keyEvent->modifiers() & Qt::AltModifier) {
                QGuiApplication::sendEvent(this, keyEvent);
                return true;
            }
        }
    }

    if (watched == ui_.pageNumberLabel && event->type() == QEvent::MouseButtonPress) {
        ScrollItemsWidget *board = currItemsWidget();
        const int totalPages = board ? board->totalPageCount() : 0;
        const int currentPage = board ? board->currentPageNumber() : 1;
        if (totalPages > 1) {
            auto *popup = new GlassPagePopup(this);
            popup->setAttribute(Qt::WA_DeleteOnClose);
            popup->setDark(darkTheme_);
            popup->setPages(totalPages, currentPage);
            connect(popup, &GlassPagePopup::pageSelected, this, [this](int page) {
                ui_.pageComboBox->setCurrentIndex(page - 1);
            });
            const QPoint pos = ui_.pageNumberLabel->mapToGlobal(
                QPoint(0, -popup->sizeHint().height() - 4));
            popup->popup(QPoint(pos.x() - popup->width() / 2 + ui_.pageNumberLabel->width() / 2,
                                ui_.pageNumberLabel->mapToGlobal(QPoint(0, 0)).y() - popup->height() - 4));
        }
        return true;
    }

    return QObject::eventFilter(watched, event);
}

void MPasteWidget::keyPressEvent(QKeyEvent *event) {
    if (event->modifiers() & Qt::AltModifier) {
        const int shortcutIndex = shortcutIndexForKey(event->key());
        if (shortcutIndex >= 0) {
            if (event->isAutoRepeat()) {
                event->accept();
                return;
            }

            misc_.pendingNumKey = shortcutIndex;
            misc_.pendingPlainTextNumKey = event->modifiers().testFlag(Qt::ShiftModifier);
            triggerShortcutPaste(shortcutIndex, misc_.pendingPlainTextNumKey);
            event->accept();
            return;
        }
    }
    handleKeyboardEvent(event);
}



bool MPasteWidget::focusNextPrevChild(bool next) {
    return false;
}

void MPasteWidget::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt) {
        currItemsWidget()->cleanShortCutInfo();
        misc_.pendingNumKey = -1;
        misc_.pendingPlainTextNumKey = false;
    }
    else {
        const int releasedShortcutIndex = shortcutIndexForKey(event->key());
        if (releasedShortcutIndex == misc_.pendingNumKey && misc_.pendingNumKey >= 0) {
            misc_.pendingNumKey = -1;
            misc_.pendingPlainTextNumKey = false;
            event->accept();
            return;
        }
    }

    QWidget::keyReleaseEvent(event);
}


void MPasteWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
}

void MPasteWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal radius = 8.0;
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // Clear to transparent so DWM acrylic shows through
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (darkTheme_) {
        // Glass edge - thin luminous white border
        QPen glassPen(QColor(255, 255, 255, 40), 1.5);
        p.setPen(glassPen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, radius, radius);

        // Specular highlight arc at top
        QLinearGradient specular(r.topLeft(), QPointF(r.left(), r.top() + r.height() * 0.15));
        specular.setColorAt(0.0, QColor(255, 255, 255, 18));
        specular.setColorAt(1.0, QColor(255, 255, 255, 0));
        QPainterPath topClip;
        topClip.addRoundedRect(r.adjusted(1.5, 1.5, -1.5, 0), radius - 1, radius - 1);
        p.setClipPath(topClip);
        p.fillRect(QRectF(r.left(), r.top(), r.width(), r.height() * 0.15), specular);
        p.setClipping(false);

        // Inner edge glow
        QPen innerGlow(QColor(255, 255, 255, 20), 0.5);
        p.setPen(innerGlow);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(2, 2, -2, -2), radius - 1.5, radius - 1.5);
    } else {
        // Light: gradient border (original style)
        const qreal bw = 3.0;
        QRectF rb = QRectF(rect()).adjusted(bw / 2.0, bw / 2.0, -bw / 2.0, -bw / 2.0);
        QConicalGradient grad(rb.center(), 135);
        grad.setColorAt(0.00, QColor("#4A90E2"));
        grad.setColorAt(0.25, QColor("#1abc9c"));
        grad.setColorAt(0.50, QColor("#fc9867"));
        grad.setColorAt(0.75, QColor("#9B59B6"));
        grad.setColorAt(1.00, QColor("#4A90E2"));
        p.setPen(QPen(QBrush(grad), bw));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rb, radius, radius);
    }
}

void MPasteWidget::showEvent(QShowEvent *event) {
    QElapsedTimer t; t.start();
    QWidget::showEvent(event);
    qInfo().noquote() << QStringLiteral("[wake] showEvent: QWidget::showEvent %1 ms").arg(t.elapsed());
    stopKeepAliveTimer();
    qInfo().noquote() << QStringLiteral("[wake] showEvent: stopKeepAliveTimer %1 ms").arg(t.elapsed());
    activateWindow();
    raise();
    setFocus();
    qInfo().noquote() << QStringLiteral("[wake] showEvent: activate/raise/focus %1 ms").arg(t.elapsed());
    if (syncWatcher_ && syncWatcher_->checkPendingReload()) {
        syncHistoryBoardsIncremental();
        qInfo().noquote() << QStringLiteral("[wake] showEvent: syncIncremental %1 ms").arg(t.elapsed());
    }
    // Show "Loading..." if data is still being loaded from disk,
    // so the panel is not blank on very early wake.
    if (auto *board = currItemsWidget()) {
        board->updateLoadingOverlay();
    }
    qInfo().noquote() << QStringLiteral("[wake] showEvent total: %1 ms").arg(t.elapsed());
}

void MPasteWidget::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    for (auto it = ui_.boardWidgetMap.cbegin(); it != ui_.boardWidgetMap.cend(); ++it) {
        if (auto *board = it.value()) {
            board->hideHoverTools();
        }
    }
    startKeepAliveTimer();
}

void MPasteWidget::setupSyncWatcher() {
    const QString rootDir = QDir::cleanPath(MPasteSettings::getInst()->getSaveDir());
    if (rootDir.isEmpty()) {
        return;
    }

    if (!syncWatcher_) {
        syncWatcher_ = new SyncWatcher(this);
        connect(syncWatcher_, &SyncWatcher::syncRequested, this, [this]() {
            if (!isVisible()) {
                syncWatcher_->setPendingReload();
                return;
            }
            syncHistoryBoardsIncremental();
        });
    }

    // Provide board services so internal-write detection works.
    syncWatcher_->setBoardServices(
        ui_.clipboardWidget ? ui_.clipboardWidget->boardServiceRef() : nullptr,
        ui_.staredWidget ? ui_.staredWidget->boardServiceRef() : nullptr);

    const QStringList categoryDirs = {
        QDir::cleanPath(rootDir + QDir::separator() + MPasteSettings::CLIPBOARD_CATEGORY_NAME),
        QDir::cleanPath(rootDir + QDir::separator() + MPasteSettings::STAR_CATEGORY_NAME)
    };
    syncWatcher_->setup(rootDir, categoryDirs);
}

void MPasteWidget::loadFromSaveDir() {
    ui_.clipboardWidget->setFavoriteFingerprints(ui_.staredWidget->loadAllFingerprints());
    ui_.staredWidget->loadFromSaveDirDeferred();
    ui_.clipboardWidget->loadFromSaveDirDeferred();
}

void MPasteWidget::reloadHistoryBoards() {
    const QString keyword = ui_.ui->searchEdit->text();
    const auto type = static_cast<ContentType>(
        ui_.typeButtonGroup->checkedButton()
            ? ui_.typeButtonGroup->checkedButton()->property("contentType").toInt()
            : static_cast<int>(All));

    loadFromSaveDir();
    ui_.clipboardWidget->filterByType(type);
    ui_.clipboardWidget->filterByKeyword(keyword);
    ui_.staredWidget->filterByType(type);
    ui_.staredWidget->filterByKeyword(keyword);
    updateItemCount(currItemsWidget()->getItemCount());
    updatePageSelector();
}

void MPasteWidget::syncHistoryBoardsIncremental() {
    ui_.clipboardWidget->syncFromDiskIncremental();
    ui_.staredWidget->syncFromDiskIncremental();
    updateItemCount(currItemsWidget()->getItemCount());
    updatePageSelector();
}

void MPasteWidget::setFocusOnSearch(bool flag) {
    if (flag) {
        ui_.ui->searchEdit->show();
        ui_.searchShowAnim->start();
        ui_.ui->searchEdit->setFocus();
    } else {
        ui_.searchHideAnim->start();
        ui_.ui->searchEdit->clearFocus();
        setFocus();
    }
}

ScrollItemsWidget *MPasteWidget::currItemsWidget() {
    if (!ui_.buttonGroup) {
        return ui_.clipboardWidget;
    }

    QAbstractButton* currentBtn = ui_.buttonGroup->checkedButton();
    if (currentBtn) {
        QString category = currentBtn->property("category").toString();
        return ui_.boardWidgetMap[category];
    }

    return ui_.clipboardWidget;
}

void MPasteWidget::updatePageSelectorStyle() {
    if (!ui_.pageSelectorWidget || !ui_.pagePrefixLabel || !ui_.pageComboBox || !ui_.pageTotalLabel || !ui_.pageSuffixLabel) {
        return;
    }

    const int height = qMax(24, ui_.pageSelectorWidget->height());
    const int radius = qMax(12, height / 2);
    const QString background = darkTheme_
        ? QStringLiteral("rgba(28, 34, 44, 235)")
        : QStringLiteral("rgba(255, 255, 255, 242)");
    const QString border = QStringLiteral("#4A90E2");
    const QString text = darkTheme_ ? QStringLiteral("#F2F6FB") : QStringLiteral("#2C3E50");
    const QString divider = darkTheme_ ? QStringLiteral("rgba(242, 246, 251, 110)")
                                       : QStringLiteral("rgba(44, 62, 80, 96)");
    if (auto *pill = static_cast<PageSelectorPill *>(ui_.pageSelectorWidget)) {
        ui_.pageSelectorWidget->layout()->activate();
        pill->setColors(QColor(background), QColor(border), QColor(divider));
        pill->setCornerRadius(radius);
        pill->setDividerInset(3);
        pill->setDividerPosition(ui_.pageNumberLabel->geometry().right() + 4);
        pill->setPaintFrame(false);
    }
    const QString hoverBg = darkTheme_ ? QStringLiteral("rgba(255, 255, 255, 22)") : QStringLiteral("rgba(0, 0, 0, 12)");
    const QString hoverColor = darkTheme_ ? QStringLiteral("#4A90E2") : QStringLiteral("#2A6FC9");
    ui_.pageNumberLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: 700; background: transparent;"
        " border-radius: 4px; padding: 1px 3px; }"
        "QLabel:hover { color: %2; background: %3; }").arg(text, hoverColor, hoverBg));
    const QString secondary = darkTheme_ ? QStringLiteral("rgba(242, 246, 251, 185)")
                                         : QStringLiteral("rgba(44, 62, 80, 185)");
    ui_.pagePrefixLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent; }").arg(secondary));
    ui_.pageTotalLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: 700; background: transparent; }").arg(secondary));
    ui_.pageSuffixLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent; }").arg(secondary));
}

void MPasteWidget::updatePageSelector() {
    if (!ui_.pageSelectorWidget || !ui_.pagePrefixLabel || !ui_.pageComboBox || !ui_.pageTotalLabel || !ui_.pageSuffixLabel) {
        return;
    }

    const bool pagedMode = MPasteSettings::getInst()->getHistoryViewMode() == MPasteSettings::ViewModePaged;
    ui_.pageSelectorWidget->setVisible(pagedMode);
    if (!pagedMode) {
        return;
    }

    ScrollItemsWidget *board = currItemsWidget();
    const int totalPages = board ? board->totalPageCount() : 0;
    const int currentPage = board ? board->currentPageNumber() : 0;

    const int clampedTotalPages = qMax(1, totalPages);
    ui_.pageComboBox->blockSignals(true);
    if (ui_.pageComboBox->count() != clampedTotalPages) {
        ui_.pageComboBox->clear();
        for (int page = 1; page <= clampedTotalPages; ++page) {
            ui_.pageComboBox->addItem(QString::number(page), page);
        }
    }
    ui_.pageComboBox->setCurrentIndex(qBound(0, (currentPage > 0 ? currentPage : 1) - 1, clampedTotalPages - 1));
    ui_.pageComboBox->blockSignals(false);
    ui_.pageTotalLabel->setText(QStringLiteral("/ %1").arg(totalPages));

    const int displayPage = qBound(1, currentPage > 0 ? currentPage : 1, clampedTotalPages);
    ui_.pageNumberLabel->setText(QString::number(displayPage));

    const QFontMetrics labelMetrics(ui_.pageNumberLabel->font());
    const int labelWidth = qMax(20, labelMetrics.horizontalAdvance(QString::number(clampedTotalPages)) + 8);
    ui_.pageNumberLabel->setFixedWidth(labelWidth);

    const QFontMetrics totalMetrics(ui_.pageTotalLabel->font());
    const int totalWidth = qMax(26, totalMetrics.horizontalAdvance(QStringLiteral("/ %1").arg(totalPages)) + 4);
    ui_.pageTotalLabel->setFixedWidth(totalWidth);

    if (ui_.pageSelectorWidget && ui_.pageSelectorWidget->layout()) {
        const QMargins margins = ui_.pageSelectorWidget->layout()->contentsMargins();
        const int spacing = ui_.pageSelectorWidget->layout()->spacing();
        ui_.pageSelectorWidget->setFixedWidth(margins.left()
                                              + ui_.pagePrefixLabel->width()
                                              + spacing
                                              + labelWidth
                                              + spacing
                                              + totalWidth
                                              + spacing
                                              + ui_.pageSuffixLabel->width()
                                              + margins.right());
    }
}

void MPasteWidget::updateItemCount(int itemCount) {
    const int selectedCount = currItemsWidget() ? currItemsWidget()->selectedItemCount() : 0;
    ui_.ui->countArea->setText(selectedCount > 1
        ? QStringLiteral("%1/%2").arg(selectedCount).arg(itemCount)
        : QString::number(itemCount));
    ui_.ui->countArea->updateGeometry();
}

void MPasteWidget::hideAndPaste() {
    WId previousWId = PlatformRelated::previousActiveWindow();

    hide();

    pasteController_->pasteToTarget(previousWId);
}
void MPasteWidget::setVisibleWithAnnimation(bool visible) {
    if (visible == isVisible()) return;

    if (visible) {
        QElapsedTimer t; t.start();
        setWindowOpacity(0);
        show();
        qInfo().noquote() << QStringLiteral("[wake] setVisibleWithAnnimation: show() took %1 ms").arg(t.elapsed());
        if (clipboard_.copiedWhenHide) {
            ui_.clipboardWidget->scrollToFirst();
            clipboard_.copiedWhenHide = false;
        }

        QPropertyAnimation* animation = new QPropertyAnimation(this, "windowOpacity", this);
        animation->setDuration(200);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);

        connect(animation, &QPropertyAnimation::finished, this, [this, animation]() {
            animation->deleteLater();
            if (!ui_.ui->searchEdit->text().isEmpty()) {
                ui_.ui->searchEdit->setFocus();
            }

            for (int i = 0; i < 10; ++i) {
                if (PlatformRelated::currActiveWindow() == winId()) {
                    break;
                }
                PlatformRelated::activateWindow(winId());
            }
        });

        animation->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        QPropertyAnimation* animation = new QPropertyAnimation(this, "windowOpacity", this);
        animation->setDuration(HIDE_ANIMATION_TIME);
        animation->setStartValue(1.0);
        animation->setEndValue(0.0);
        animation->setEasingCurve(QEasingCurve::InCubic);

        connect(animation, &QPropertyAnimation::finished, this, [this, animation]() {
            animation->deleteLater();
            hide();
            currItemsWidget()->cleanShortCutInfo();
        });

        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void MPasteWidget::startKeepAliveTimer() {
#ifdef Q_OS_WIN
    // Tell Windows to keep a reasonable minimum working set so that the
    // process memory is not aggressively paged out during long idle periods.
    // This is the primary mechanism to prevent the multi-second freeze when
    // the user invokes the hotkey after hours of inactivity.
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        // Use current working set as the minimum, capped to a sane range.
        const SIZE_T current = pmc.WorkingSetSize;
        const SIZE_T minWS = qBound<SIZE_T>(20ULL * 1024 * 1024, current, 200ULL * 1024 * 1024);
        const SIZE_T maxWS = qMax<SIZE_T>(minWS * 2, 400ULL * 1024 * 1024);
        SetProcessWorkingSetSize(GetCurrentProcess(), minWS, maxWS);
    }

    if (!keepAliveTimer_) {
        keepAliveTimer_ = new QTimer(this);
        keepAliveTimer_->setTimerType(Qt::VeryCoarseTimer);
        connect(keepAliveTimer_, &QTimer::timeout, this, &MPasteWidget::touchWorkingSet);
    }
    keepAliveTimer_->start(KEEPALIVE_INTERVAL_MS);
#endif
}

void MPasteWidget::stopKeepAliveTimer() {
#ifdef Q_OS_WIN
    // Restore default working set policy so the OS can reclaim memory
    // while the widget is visible and actively used.
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
#endif
    if (keepAliveTimer_) {
        keepAliveTimer_->stop();
    }
}

void MPasteWidget::touchWorkingSet() {
#ifdef Q_OS_WIN
    // Walk key data structures to fault their pages back in, preventing
    // Windows from trimming them even with the minimum working set set.
    volatile char sink = 0;

    auto touchWidget = [&sink](ScrollItemsWidget *w) {
        if (!w) return;
        auto *model = w->boardModel();
        if (!model) return;
        const int rows = model->rowCount();
        const int step = qMax(1, rows / 16);
        for (int i = 0; i < rows; i += step) {
            const ClipboardItem *item = model->itemPtrAt(i);
            if (item) {
                const QString &name = item->getName();
                if (!name.isEmpty()) {
                    sink += name.at(0).unicode();
                }
                // Touch thumbnail pixmap data if present.
                if (item->hasThumbnail()) {
                    const QPixmap &px = item->thumbnail();
                    sink += reinterpret_cast<const volatile char *>(&px)[0];
                }
            }
        }
        // Touch the widget's own object tree.
        sink += reinterpret_cast<const volatile char *>(w)[0];
    };

    touchWidget(ui_.clipboardWidget);
    touchWidget(ui_.staredWidget);

    // Touch this widget and its UI object.
    sink += reinterpret_cast<const volatile char *>(this)[0];
    if (ui_.ui) {
        sink += reinterpret_cast<const volatile char *>(ui_.ui)[0];
    }
    Q_UNUSED(sink);
#endif
}

void MPasteWidget::dumpMemoryStats() {
    qInfo().noquote() << QStringLiteral("===== MPaste Memory Stats =====");
    if (ui_.clipboardWidget) {
        qInfo().noquote() << ui_.clipboardWidget->memoryStats();
    }
    if (ui_.staredWidget) {
        qInfo().noquote() << ui_.staredWidget->memoryStats();
    }
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        qInfo().noquote() << QStringLiteral("[process] workingSet: %1 MB, peakWorkingSet: %2 MB, pagefile: %3 MB")
                                .arg(pmc.WorkingSetSize / (1024 * 1024))
                                .arg(pmc.PeakWorkingSetSize / (1024 * 1024))
                                .arg(pmc.PagefileUsage / (1024 * 1024));
    }
#endif
    qInfo().noquote() << QStringLiteral("===============================");
}

void MPasteWidget::debugKeyState() {
#ifdef Q_OS_WIN
    qDebug() << "Alt Key State:" << (GetAsyncKeyState(VK_MENU) & 0x8000)
             << "Window Focus:" << hasFocus()
             << "Window ID:" << winId()
             << "Is Visible:" << isVisible()
             << "Active Window:" << QApplication::activeWindow();
#endif
}

void MPasteWidget::ensureOcrService() {
    if (ocrService_) {
        return;
    }
    ocrService_ = new OcrService(this);
    connect(ocrService_, &OcrService::ocrFinished,
            this, [this](const QString &itemName, const OcrService::Result &result) {
        qInfo() << "[ocr] finished item=" << itemName
                << "status=" << result.status
                << "textLen=" << result.text.length()
                << "error=" << result.errorMessage;
        if (result.status == OcrService::Ready) {
            bool written = false;
            for (auto *w : ui_.boardWidgetMap.values()) {
                auto *bs = w->boardServiceRef();
                if (!bs) continue;
                const QString filePath = bs->filePathForName(itemName);
                if (QFile::exists(filePath)) {
                    OcrService::writeSidecar(filePath, result);
                    bs->refreshIndexedItemForPath(filePath);
                    written = true;
                }
            }
            // If the .mpaste file hasn't been saved yet (async save
            // in progress), retry after a short delay.
            if (!written && !result.text.isEmpty()) {
                QTimer::singleShot(2000, this, [this, itemName, result]() {
                    for (auto *w : ui_.boardWidgetMap.values()) {
                        auto *bs = w->boardServiceRef();
                        if (!bs) continue;
                        const QString filePath = bs->filePathForName(itemName);
                        if (QFile::exists(filePath)) {
                            OcrService::writeSidecar(filePath, result);
                            bs->refreshIndexedItemForPath(filePath);
                        }
                    }
                });
            }
        }
        // Clear OCR-pending indicator on the card.
        for (auto *w : ui_.boardWidgetMap.values()) {
            if (auto *d = w->cardDelegateRef()) {
                d->clearOcrPending(itemName);
            }
        }
        const bool manual = manualOcrItems_.remove(itemName);
        if (manual) {
            if (ocrLoadingDialog_) {
                ocrLoadingDialog_->close();
                ocrLoadingDialog_ = nullptr;
            }
            if (result.status != OcrService::Ready || result.text.isEmpty()) {
                const QString msg = result.status == OcrService::Failed
                    ? result.errorMessage
                    : tr("No text recognized in this image.");
                QMessageBox::warning(this, tr("OCR"), msg);
                return;
            }
            showOcrResultDialog(result.text);
        }
    });
}

void MPasteWidget::showOcrResultDialog(const QString &text) {
    const bool dark = MPasteSettings::getInst()->isDarkTheme();

    auto *dialog = new OcrResultDialog(this);
    dialog->dark = dark;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAttribute(Qt::WA_TranslucentBackground);
    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    dialog->setModal(false);
    dialog->resize(720, 520);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(12);

    // Header
    auto *headerLayout = new QHBoxLayout;
    auto *titleLabel = new QLabel(tr("Recognized Text"), dialog);
    titleLabel->setObjectName(QStringLiteral("previewTitle"));
    headerLayout->addWidget(titleLabel, 1);

    auto *copyBtn = new QToolButton(dialog);
    copyBtn->setObjectName(QStringLiteral("closeButton"));
    copyBtn->setText(QStringLiteral("\u2398")); // copy icon
    copyBtn->setToolTip(tr("Copy to Clipboard"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    connect(copyBtn, &QToolButton::clicked, dialog, [text, copyBtn]() {
        QGuiApplication::clipboard()->setText(text);
        copyBtn->setText(QStringLiteral("\u2713"));
        QTimer::singleShot(1200, copyBtn, [copyBtn]() {
            if (copyBtn) copyBtn->setText(QStringLiteral("\u2398"));
        });
    });
    headerLayout->addWidget(copyBtn, 0, Qt::AlignTop);

    auto *closeBtn = new QToolButton(dialog);
    closeBtn->setObjectName(QStringLiteral("closeButton"));
    closeBtn->setText(QStringLiteral("\u00D7"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, dialog, &QDialog::close);
    headerLayout->addWidget(closeBtn, 0, Qt::AlignTop);

    layout->addLayout(headerLayout);

    // Text content — all widgets must have transparent backgrounds
    // so the DWM acrylic blur shows through.
    auto *textEdit = new QTextEdit(dialog);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(text);
    textEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    textEdit->setFrameShape(QFrame::NoFrame);
    textEdit->setObjectName(QStringLiteral("ocrTextEdit"));
    layout->addWidget(textEdit, 1);

    // Stylesheet — all backgrounds transparent, only blur provides bg.
    const QString textColor = dark ? QStringLiteral("#E6EDF5") : QStringLiteral("#1E2936");
    const QString subtextColor = dark ? QStringLiteral("#9AA7B5") : QStringLiteral("#5E7084");
    const QString borderColor = dark ? QStringLiteral("rgba(255,255,255,25)")
                                     : QStringLiteral("rgba(0,0,0,18)");
    const QString btnBg = dark ? QStringLiteral("rgba(255,255,255,14)")
                               : QStringLiteral("rgba(0,0,0,8)");
    const QString btnHoverBg = dark ? QStringLiteral("rgba(255,255,255,28)")
                                    : QStringLiteral("rgba(0,0,0,16)");
    const QString selBg = dark ? QStringLiteral("rgba(74,144,226,110)")
                               : QStringLiteral("rgba(74,144,226,76)");

    dialog->setStyleSheet(QStringLiteral(
        "QLabel#previewTitle {"
        "  color: %1; font-size: 22px; font-weight: 700; background: transparent;"
        "}"
        "QTextEdit#ocrTextEdit {"
        "  background-color: transparent; border: none;"
        "  color: %1; font-size: 15px;"
        "  selection-background-color: %2;"
        "  padding: 8px;"
        "}"
        "QToolButton#closeButton {"
        "  background-color: %3; border: 1px solid %4;"
        "  border-radius: 14px; color: %5;"
        "  font-size: 17px; font-weight: 700;"
        "  min-width: 28px; min-height: 28px;"
        "}"
        "QToolButton#closeButton:hover {"
        "  background-color: %6; border-color: %4;"
        "}"
    ).arg(textColor, selBg, btnBg, borderColor, subtextColor, btnHoverBg));

    // Center on screen.
    QScreen *screen = nullptr;
    if (auto *w = window()->windowHandle()) {
        screen = w->screen();
    }
    if (!screen) screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect geo = screen->availableGeometry();
        dialog->move(geo.center() - QPoint(dialog->width() / 2, dialog->height() / 2));
    }

    dialog->show();
    WindowBlurHelper::enableBlurBehind(dialog, dark);
    dialog->raise();
    dialog->activateWindow();
}

#include "MPasteWidget.moc"
