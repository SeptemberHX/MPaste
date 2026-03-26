// input: Depends on MPasteWidget.h, Qt runtime services, resource assets, and platform clipboard/window helpers.
// output: Implements the main window, item interaction flow, reliable quick-paste shortcuts, and plain-text paste behavior.
// pos: Widget-layer main window implementation coordinating boards, shortcuts, and system integration.
// update: If I change, update this header block and my folder README.md.
// note: Added theme application, dark mode propagation, tray menu theming, robust paste rehydration, and alias sync.
#include <QScrollBar>
#include <QClipboard>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
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
#include <QMessageBox>
#include <QLabel>
#include <QComboBox>
#include <QStyle>
#include <QStyleFactory>
#include "utils/MPasteSettings.h"
#include "utils/ClipboardExportService.h"
#include "utils/ThemeManager.h"
#include "utils/IconResolver.h"
#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "utils/PlatformRelated.h"
#include "data/LocalSaver.h"

namespace {
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

QString menuText(const char *source, const QString &zhFallback) {
    const QString translated = QObject::tr(source);
    const QLocale locale = QLocale::system();
    if (translated == QLatin1String(source) || looksBrokenTranslation(translated)) {
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

    ui_.pageComboBox = new QComboBox(ui_.pageSelectorWidget);
    ui_.pageComboBox->setEditable(false);
    ui_.pageComboBox->setInsertPolicy(QComboBox::NoInsert);
    ui_.pageComboBox->setMaxVisibleItems(18);
    ui_.pageComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui_.pageComboBox->setMinimumContentsLength(2);
    ui_.pageComboBox->addItem(QStringLiteral("1"), 1);
    ui_.pageComboBox->setCurrentIndex(0);
    ui_.pageComboBox->setFixedWidth(38);

    ui_.pageTotalLabel = new QLabel(QStringLiteral("/ 1"), ui_.pageSelectorWidget);
    ui_.pageTotalLabel->setAlignment(Qt::AlignCenter);
    ui_.pageTotalLabel->setFixedWidth(36);

    ui_.pageSuffixLabel = new QLabel(QStringLiteral("页"), ui_.pageSelectorWidget);
    ui_.pageSuffixLabel->setAlignment(Qt::AlignCenter);
    ui_.pageSuffixLabel->setFixedWidth(14);

    pageLayout->addWidget(ui_.pagePrefixLabel);
    pageLayout->addWidget(ui_.pageComboBox);
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

    ui_.ui->allTypeBtn->setProperty("contentType", static_cast<int>(ClipboardItem::All));
    ui_.ui->textTypeBtn->setProperty("contentType", static_cast<int>(ClipboardItem::Text));
    ui_.ui->linkTypeBtn->setProperty("contentType", static_cast<int>(ClipboardItem::Link));
    ui_.ui->imageTypeBtn->setProperty("contentType", static_cast<int>(ClipboardItem::Image));
    ui_.ui->officeTypeBtn->setProperty("contentType", static_cast<int>(ClipboardItem::Office));
    ui_.ui->richTextTypeBtn->setProperty("contentType", static_cast<int>(ClipboardItem::RichText));
    ui_.ui->fileTypeBtn->setProperty("contentType", static_cast<int>(ClipboardItem::File));

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
        ui_.aboutWidget->setWindowFlag(Qt::Tool);
        ui_.aboutWidget->setWindowTitle("MPaste About");
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
        connect(ui_.settingsWidget, &MPasteSettingsWidget::previewCacheActionRequested,
                this, &MPasteWidget::runPreviewCacheAction);
        connect(ui_.settingsWidget, &MPasteSettingsWidget::thumbnailPrefetchChanged,
                this, [this](int) {
                    if (ui_.clipboardWidget) {
                        ui_.clipboardWidget->refreshThumbnailCache();
                    }
                    if (ui_.staredWidget) {
                        ui_.staredWidget->refreshThumbnailCache();
                    }
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
        auto *boardService = ui_.clipboardWidget->boardServiceRef();
        if (boardService && boardService->hasPendingItems()) {
            connect(boardService, &ClipboardBoardService::deferredLoadCompleted, this, [this]() {
                clipboard_.monitor->primeCurrentClipboard();
                qInfo().noquote() << QStringLiteral("[startup] deferred primeCurrentClipboard done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
                loading_.startupWarmupCompleted = true;
            }, Qt::SingleShotConnection);
        } else {
            clipboard_.monitor->primeCurrentClipboard();
            qInfo().noquote() << QStringLiteral("[startup] deferred primeCurrentClipboard done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
            loading_.startupWarmupCompleted = true;
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
    clipboard_.isPasting = false;
    clipboard_.copiedWhenHide = false;
}

void MPasteWidget::initShortcuts() {
    misc_.numKeyList.clear();
    misc_.numKeyList << Qt::Key_1 << Qt::Key_2 << Qt::Key_3 << Qt::Key_4 << Qt::Key_5
                     << Qt::Key_6 << Qt::Key_7 << Qt::Key_8 << Qt::Key_9 << Qt::Key_0;
}

void MPasteWidget::initSound() {
    rebuildSoundPlaybackChain(QMediaDevices::defaultAudioOutput());
}

void MPasteWidget::rebuildSoundPlaybackChain(const QAudioDevice &device) {
    if (misc_.player) {
        misc_.player->stop();
        misc_.player->setAudioOutput(nullptr);
        delete misc_.player;
        misc_.player = nullptr;
    }

    if (misc_.audioOutput) {
        delete misc_.audioOutput;
        misc_.audioOutput = nullptr;
    }

    misc_.player = new QMediaPlayer(this);
    misc_.audioOutput = new QAudioOutput(this);
    misc_.audioOutput->setDevice(device);
    misc_.player->setAudioOutput(misc_.audioOutput);
    misc_.player->setSource(QUrl(QStringLiteral("qrc:/resources/resources/sound.mp3")));
}

void MPasteWidget::syncSoundOutputDevice() {
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    if (misc_.player && misc_.audioOutput
        && misc_.audioOutput->device().id() == defaultDevice.id()) {
        return;
    }

    rebuildSoundPlaybackChain(defaultDevice);
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

    applyMenuTheme(ui_.menu);
    applyMenuTheme(ui_.trayMenu);
}


void MPasteWidget::applyTheme(bool dark) {
    darkTheme_ = dark;

#ifdef Q_OS_WIN
    const QColor tint = darkTheme_ ? QColor(12, 18, 26, 48) : QColor(231, 241, 244, 20);
    enableBlurBehind((HWND)winId(), tint);
#endif

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
    updatePageSelectorStyle();
    applyMenuTheme(ui_.menu);
    applyMenuTheme(ui_.trayMenu);
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
            if (this->setClipboard(item)) {
                this->hideAndPaste();
            }
        });

        connect(boardWidget, &ScrollItemsWidget::plainTextPasteRequested,
        this, [this](const ClipboardItem &item) {
            if (this->setClipboard(item, true)) {
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
            sync_.suppressReloadUntilMs = QDateTime::currentMSecsSinceEpoch() + 800;
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
            auto type = static_cast<ClipboardItem::ContentType>(button->property("contentType").toInt());
            this->currItemsWidget()->filterByType(type);
        });

    connect(ui_.buttonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
        [this](QAbstractButton *button) {
            auto typeBtn = ui_.typeButtonGroup->checkedButton();
            auto type = typeBtn ? static_cast<ClipboardItem::ContentType>(typeBtn->property("contentType").toInt())
                                : ClipboardItem::All;

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

void MPasteWidget::playCopySoundIfNeeded(int wId, const QByteArray &fingerprint) {
    if (!MPasteSettings::getInst()->isPlaySound()) {
        qInfo() << "[clipboard-widget] play sound disabled";
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - misc_.lastSoundPlayAtMs < SOUND_BURST_WINDOW_MS) {
        qInfo().noquote() << QStringLiteral("[clipboard-widget] suppress sound by burst window wId=%1 fp=%2 deltaMs=%3")
            .arg(wId)
            .arg(fingerprint.isEmpty() ? QStringLiteral("-") : QString::fromLatin1(fingerprint.toHex().left(12)))
            .arg(now - misc_.lastSoundPlayAtMs);
        return;
    }

    syncSoundOutputDevice();
    if (misc_.player->mediaStatus() == QMediaPlayer::EndOfMedia) {
        misc_.player->setPosition(0);
    }
    if (misc_.player->playbackState() == QMediaPlayer::PlayingState) {
        misc_.player->stop();
    }
    misc_.player->play();
    misc_.lastSoundPlayAtMs = now;
    qInfo().noquote() << QStringLiteral("[clipboard-widget] play copy sound wId=%1 fp=%2")
        .arg(wId)
        .arg(fingerprint.isEmpty() ? QStringLiteral("-") : QString::fromLatin1(fingerprint.toHex().left(12)));
}

void MPasteWidget::clipboardActivityObserved(int wId) {
    if (clipboard_.isPasting) {
        return;
    }
    playCopySoundIfNeeded(wId);
}

void MPasteWidget::clipboardUpdated(const ClipboardItem &nItem, int wId) {
    if (!clipboard_.isPasting) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool added = ui_.clipboardWidget->addAndSaveItem(nItem);
        qInfo().noquote() << QStringLiteral("[clipboard-widget] clipboardUpdated wId=%1 isPasting=%2 added=%3 sinceLastSoundMs=%4 %5")
            .arg(wId)
            .arg(clipboard_.isPasting)
            .arg(added)
            .arg(now - misc_.lastSoundPlayAtMs)
            .arg(widgetItemSummary(nItem));

        if (added) {
            clipboard_.copiedWhenHide = true;
        }
    }
}

QMimeData *MPasteWidget::createPlainTextMimeData(const ClipboardItem &item) const {
    QString plainText;
    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();

    if (item.getContentType() == ClipboardItem::File && !normalizedUrls.isEmpty()) {
        QStringList urls;
        for (const QUrl &url : normalizedUrls) {
            urls << (url.isLocalFile() ? url.toLocalFile() : url.toString());
        }
        plainText = urls.join(QLatin1Char('\n'));
    }

    if (plainText.isEmpty()) {
        plainText = item.getNormalizedText();
    }

    if (plainText.isEmpty() && item.getMimeData() && item.getMimeData()->hasHtml()) {
        QTextDocument doc;
        doc.setHtml(item.getHtml());
        plainText = doc.toPlainText();
    }

    if (plainText.isEmpty() && !normalizedUrls.isEmpty()) {
        QStringList urls;
        for (const QUrl &url : normalizedUrls) {
            urls << (url.isLocalFile() ? url.toLocalFile() : url.toString());
        }
        plainText = urls.join(QLatin1Char('\n'));
    }

    if (plainText.isEmpty() && item.getMimeData() && item.getMimeData()->hasColor()) {
        plainText = item.getColor().name(QColor::HexRgb);
    }

    if (plainText.isEmpty()) {
        return nullptr;
    }

    auto *mimeData = new QMimeData;
    mimeData->setText(plainText);
    mimeData->setData("text/plain;charset=utf-8", plainText.toUtf8());
    return mimeData;
}

bool MPasteWidget::setClipboard(const ClipboardItem &item, bool plainText) {
    qInfo().noquote() << QStringLiteral("[clipboard-widget] setClipboard begin plainText=%1 %2")
        .arg(plainText)
        .arg(widgetItemSummary(item));
    clipboard_.monitor->disconnectMonitor();

    QMimeData *mimeData = plainText ? createPlainTextMimeData(item)
                                    : ClipboardExportService::buildMimeData(item);
    if (!mimeData && !plainText) {
        const ClipboardItem rehydrated = rehydrateClipboardItem(item);
        if (!rehydrated.getName().isEmpty()) {
            mimeData = ClipboardExportService::buildMimeData(rehydrated);
            if (!mimeData) {
                mimeData = createPlainTextMimeData(rehydrated);
            }
        }
    }
    if (mimeData) {
        bool hasPayload = false;
        if (mimeData->hasText() && !mimeData->text().isEmpty()) {
            hasPayload = true;
        } else if (mimeData->hasHtml() && !mimeData->html().isEmpty()) {
            hasPayload = true;
        } else if (mimeData->hasUrls() && !mimeData->urls().isEmpty()) {
            hasPayload = true;
        } else if (mimeData->hasColor()) {
            hasPayload = true;
        } else if (mimeData->hasImage()) {
            const QVariant imageData = mimeData->imageData();
            hasPayload = imageData.isValid() && !imageData.isNull();
        } else {
            for (const QString &format : mimeData->formats()) {
                if (!mimeData->data(format).isEmpty()) {
                    hasPayload = true;
                    break;
                }
            }
        }

        if (!hasPayload) {
            delete mimeData;
            mimeData = createPlainTextMimeData(item);
        }
    }
    if (mimeData && !plainText) {
        const QString normalizedText = item.getNormalizedText();
        const bool hasText = mimeData->hasText() && !mimeData->text().isEmpty();
        const bool hasHtml = mimeData->hasHtml() && !mimeData->html().isEmpty();
        const bool hasUrls = mimeData->hasUrls() && !mimeData->urls().isEmpty();
        if (!normalizedText.isEmpty() && !hasText && !hasHtml && !hasUrls) {
            mimeData->setText(normalizedText);
            mimeData->setData("text/plain;charset=utf-8", normalizedText.toUtf8());
        }
    }
    if (!mimeData) {
        qInfo() << "[clipboard-widget] setClipboard aborted: no mimeData";
        clipboard_.monitor->connectMonitor();
        return false;
    }

    if (!plainText && item.getContentType() == ClipboardItem::File) {
        handleUrlsClipboard(mimeData, item);
    }

    QGuiApplication::clipboard()->setMimeData(mimeData);
    qInfo() << "[clipboard-widget] setClipboard wrote system clipboard";
    QTimer::singleShot(200, this, [this]() {
        qInfo() << "[clipboard-widget] reconnect monitor after self clipboard write";
        clipboard_.monitor->connectMonitor();
    });
    return true;
}
void MPasteWidget::handleUrlsClipboard(QMimeData *mimeData, const ClipboardItem &item) {
    if (!mimeData) {
        return;
    }

    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();
    if (normalizedUrls.isEmpty()) {
        return;
    }

    bool files = true;
    for (const QUrl &url : normalizedUrls) {
        if (!url.isLocalFile() || !QFileInfo::exists(url.toLocalFile())) {
            files = false;
            break;
        }
    }

    if (files) {
        QByteArray nautilus("x-special/nautilus-clipboard\n");
        QByteArray byteArray("copy\n");
        QStringList plainTextLines;
        for (const QUrl &url : normalizedUrls) {
            byteArray.append(url.toEncoded()).append('\n');
            plainTextLines << url.toLocalFile();
        }
        mimeData->setData("x-special/gnome-copied-files", byteArray);
        nautilus.append(byteArray);
        mimeData->setData("COMPOUND_TEXT", nautilus);
        const QString plainText = plainTextLines.join(QLatin1Char('\n'));
        mimeData->setText(plainText);
        mimeData->setData("text/plain;charset=utf-8", plainText.toUtf8());
    }
    mimeData->setUrls(normalizedUrls);
}
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
    if (selectedItem && setClipboard(*selectedItem, plainText)) {
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
    if (!selectedItem || !setClipboard(*selectedItem, plainText)) {
        return false;
    }

    QTimer::singleShot(50, this, [this, board]() {
        hideAndPaste();
        board->moveSelectedToFirst();
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

        const qreal bw = 2.0;
        const qreal radius = 13.0;
        QRectF r = QRectF(w->rect()).adjusted(bw / 2, bw / 2, -bw / 2, -bw / 2);

        QConicalGradient grad(r.center(), 135);
        grad.setColorAt(0.00, QColor("#4A90E2"));
        grad.setColorAt(0.25, QColor("#1abc9c"));
        grad.setColorAt(0.50, QColor("#fc9867"));
        grad.setColorAt(0.75, QColor("#9B59B6"));
        grad.setColorAt(1.00, QColor("#4A90E2"));

        p.setPen(QPen(QBrush(grad), bw));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, radius, radius);
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

    const qreal bw = 3.0;
    const qreal radius = 10.0;
    QRectF r = QRectF(rect()).adjusted(bw / 2.0, bw / 2.0, -bw / 2.0, -bw / 2.0);

    QPainterPath path;
    path.addRoundedRect(r, radius, radius);

    // Gradient border
    QConicalGradient grad(r.center(), 135);
    grad.setColorAt(0.00, QColor("#4A90E2"));
    grad.setColorAt(0.25, QColor("#1abc9c"));
    grad.setColorAt(0.50, QColor("#fc9867"));
    grad.setColorAt(0.75, QColor("#9B59B6"));
    grad.setColorAt(1.00, QColor("#4A90E2"));

    p.setPen(QPen(QBrush(grad), bw));

    // Clear to transparent first so DWM glass/acrylic shows through
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // Light tint overlay on top of the acrylic blur
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void MPasteWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    qInfo().noquote() << QStringLiteral("[startup] MPasteWidget showEvent elapsedMs=%1 visible=%2")
        .arg(misc_.startupPerfTimer.isValid() ? misc_.startupPerfTimer.elapsed() : -1)
        .arg(isVisible());
    activateWindow();
    raise();
    setFocus();
    if (sync_.pendingReload) {
        sync_.pendingReload = false;
        scheduleSyncReload();
    }
}

void MPasteWidget::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    for (auto it = ui_.boardWidgetMap.cbegin(); it != ui_.boardWidgetMap.cend(); ++it) {
        if (auto *board = it.value()) {
            board->hideHoverTools();
        }
    }
}

void MPasteWidget::setupSyncWatcher() {
    const QString rootDir = QDir::cleanPath(MPasteSettings::getInst()->getSaveDir());
    if (rootDir.isEmpty()) {
        return;
    }

    if (!sync_.watcher) {
        sync_.watcher = new QFileSystemWatcher(this);
        connect(sync_.watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
            scheduleSyncReload();
        });
    }

    if (!sync_.reloadTimer) {
        sync_.reloadTimer = new QTimer(this);
        sync_.reloadTimer->setSingleShot(true);
        sync_.reloadTimer->setInterval(400);
        connect(sync_.reloadTimer, &QTimer::timeout, this, [this]() {
            if (!isVisible()) {
                sync_.pendingReload = true;
                return;
            }
            reloadHistoryBoards();
        });
    }

    QDir().mkpath(rootDir);
    const QStringList watchDirs = {
        rootDir,
        QDir::cleanPath(rootDir + QDir::separator() + MPasteSettings::CLIPBOARD_CATEGORY_NAME),
        QDir::cleanPath(rootDir + QDir::separator() + MPasteSettings::STAR_CATEGORY_NAME)
    };
    for (const QString &dir : watchDirs) {
        QDir().mkpath(dir);
    }

    const QStringList existing = sync_.watcher->directories();
    if (!existing.isEmpty()) {
        sync_.watcher->removePaths(existing);
    }
    sync_.watcher->addPaths(watchDirs);
}

void MPasteWidget::scheduleSyncReload() {
    if (!sync_.reloadTimer) {
        return;
    }
    // Ignore directory changes caused by our own file writes.  Each board
    // service tracks its last write timestamp; if any service wrote recently,
    // this change is internal, not an external sync event.
    if ((ui_.clipboardWidget && ui_.clipboardWidget->boardServiceRef()
            && ui_.clipboardWidget->boardServiceRef()->hasRecentInternalWrite())
        || (ui_.staredWidget && ui_.staredWidget->boardServiceRef()
            && ui_.staredWidget->boardServiceRef()->hasRecentInternalWrite())) {
        return;
    }
    if (!isVisible()) {
        sync_.pendingReload = true;
        return;
    }
    sync_.reloadTimer->start();
}

void MPasteWidget::loadFromSaveDir() {
    ui_.clipboardWidget->setFavoriteFingerprints(ui_.staredWidget->loadAllFingerprints());
    ui_.staredWidget->loadFromSaveDirDeferred();
    ui_.clipboardWidget->loadFromSaveDirDeferred();
}

void MPasteWidget::runPreviewCacheAction(MPasteSettingsWidget::PreviewCacheAction action) {
    ScrollItemsWidget *boardWidget = currItemsWidget();
    if (!boardWidget) {
        return;
    }

    ClipboardBoardService::PreviewCacheMaintenanceMode mode = ClipboardBoardService::RepairBrokenPreviews;
    QString actionLabel;
    switch (action) {
        case MPasteSettingsWidget::RepairBrokenPreviews:
            mode = ClipboardBoardService::RepairBrokenPreviews;
            actionLabel = menuText("Repair broken previews", QString::fromUtf16(u"\u4FEE\u590D\u635F\u574F\u9884\u89C8"));
            break;
        case MPasteSettingsWidget::RebuildCurrentPreviews:
            mode = ClipboardBoardService::RebuildAllPreviews;
            actionLabel = menuText("Rebuild current previews", QString::fromUtf16(u"\u91CD\u5EFA\u5F53\u524D\u9884\u89C8"));
            break;
        case MPasteSettingsWidget::ClearCurrentPreviews:
            mode = ClipboardBoardService::ClearAllPreviews;
            actionLabel = menuText("Clear current previews", QString::fromUtf16(u"\u6E05\u7A7A\u5F53\u524D\u9884\u89C8"));
            break;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const int changedCount = boardWidget->maintainPreviewCache(mode);
    QApplication::restoreOverrideCursor();

    reloadHistoryBoards();

    const QString boardLabel = ui_.buttonGroup && ui_.buttonGroup->checkedButton()
        ? ui_.buttonGroup->checkedButton()->text()
        : boardWidget->getCategory();
    const QLocale locale = QLocale::system();
    const bool zh = locale.language() == QLocale::Chinese
        || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);
    const QString summary = changedCount > 0
        ? (zh
            ? QString::fromUtf16(u"\u5DF2\u5728 %1 \u4E2D\u66F4\u65B0 %2 \u4E2A\u6761\u76EE\u3002").arg(boardLabel).arg(changedCount)
            : QStringLiteral("Updated %1 items in %2.").arg(changedCount).arg(boardLabel))
        : (zh
            ? QString::fromUtf16(u"%1 \u4E2D\u6CA1\u6709\u9700\u8981\u66F4\u65B0\u7684\u9884\u89C8\u6761\u76EE\u3002").arg(boardLabel)
            : QStringLiteral("No preview entries needed updates in %1.").arg(boardLabel));
    QMessageBox::information(ensureSettingsWidget(),
                             menuText("Preview Cache", QString::fromUtf16(u"\u9884\u89C8\u7F13\u5B58")),
                             QStringLiteral("%1\n%2").arg(actionLabel, summary));
}

void MPasteWidget::reloadHistoryBoards() {
    const QString keyword = ui_.ui->searchEdit->text();
    const auto type = static_cast<ClipboardItem::ContentType>(
        ui_.typeButtonGroup->checkedButton()
            ? ui_.typeButtonGroup->checkedButton()->property("contentType").toInt()
            : static_cast<int>(ClipboardItem::All));

    loadFromSaveDir();
    ui_.clipboardWidget->filterByType(type);
    ui_.clipboardWidget->filterByKeyword(keyword);
    ui_.staredWidget->filterByType(type);
    ui_.staredWidget->filterByKeyword(keyword);
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
    const QString arrow = darkTheme_
        ? QStringLiteral(":/resources/resources/page_chevron_light.svg")
        : QStringLiteral(":/resources/resources/page_chevron.svg");
    if (auto *pill = static_cast<PageSelectorPill *>(ui_.pageSelectorWidget)) {
        ui_.pageSelectorWidget->layout()->activate();
        pill->setColors(QColor(background), QColor(border), QColor(divider));
        pill->setCornerRadius(radius);
        pill->setDividerInset(3);
        pill->setDividerPosition(ui_.pageComboBox->geometry().right() + 4);
        pill->setPaintFrame(false);
    }
    ui_.pageComboBox->setStyleSheet(QStringLiteral(
        "QComboBox {"
        " background: transparent;"
        " border: none;"
        " color: %1;"
        " padding: 0;"
        " font-size: 13px;"
        " font-weight: 700;"
        "}"
        "QComboBox::drop-down {"
        " subcontrol-origin: padding;"
        " subcontrol-position: top right;"
        " width: 6px;"
        " border: none;"
        " background: transparent;"
        "}"
        "QComboBox::down-arrow {"
        " width: 5px;"
        " height: 3px;"
        " image: url(%2);"
        "}"
        "QComboBox QAbstractItemView {"
        " background-color: %3;"
        " border: 1px solid %4;"
        " color: %5;"
        " selection-background-color: %6;"
        " selection-color: %5;"
        "}").arg(text, arrow,
                 darkTheme_ ? QStringLiteral("#1E232B") : QStringLiteral("#FFFFFF"),
                 darkTheme_ ? QStringLiteral("#2C3440") : QStringLiteral("#E5E5E5"),
                 darkTheme_ ? QStringLiteral("#F2F6FB") : QStringLiteral("#1A1A1A"),
                 darkTheme_ ? QStringLiteral("rgba(28, 34, 44, 235)") : QStringLiteral("#E5E5E5")));
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

    const QFontMetrics comboMetrics(ui_.pageComboBox->font());
    const int comboTextWidth = comboMetrics.horizontalAdvance(QString::number(clampedTotalPages));
    const int comboWidth = qMax(24, comboTextWidth + 12);
    ui_.pageComboBox->setFixedWidth(comboWidth);

    const QFontMetrics totalMetrics(ui_.pageTotalLabel->font());
    const int totalWidth = qMax(26, totalMetrics.horizontalAdvance(QStringLiteral("/ %1").arg(totalPages)) + 4);
    ui_.pageTotalLabel->setFixedWidth(totalWidth);

    if (ui_.pageSelectorWidget && ui_.pageSelectorWidget->layout()) {
        const QMargins margins = ui_.pageSelectorWidget->layout()->contentsMargins();
        const int spacing = ui_.pageSelectorWidget->layout()->spacing();
        ui_.pageSelectorWidget->setFixedWidth(margins.left()
                                              + ui_.pagePrefixLabel->width()
                                              + spacing
                                              + comboWidth
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

    if (!MPasteSettings::getInst()->isAutoPaste()) {
        return;
    }

    clipboard_.isPasting = true;

    auto finishPaste = [this]() {
        PlatformRelated::triggerPasteShortcut(MPasteSettings::getInst()->getPasteShortcutMode());
        QTimer::singleShot(200, this, [this]() {
            clipboard_.isPasting = false;
        });
    };

    auto restoreFocusAndPaste = [this, previousWId, finishPaste]() {
        if (previousWId) {
            PlatformRelated::activateWindow(previousWId);
            QTimer::singleShot(100, this, finishPaste);
            return;
        }

        QTimer::singleShot(0, this, finishPaste);
    };

#ifdef Q_OS_WIN
    auto *altReleaseTimer = new QTimer(this);
    auto *pollCount = new int(0);
    altReleaseTimer->setInterval(10);
    connect(altReleaseTimer, &QTimer::timeout, this, [altReleaseTimer, pollCount, restoreFocusAndPaste]() {
        const bool altReleased = (GetAsyncKeyState(VK_MENU) & 0x8000) == 0;
        const bool timedOut = *pollCount >= 50;
        if (altReleased || timedOut) {
            altReleaseTimer->stop();
            altReleaseTimer->deleteLater();
            delete pollCount;
            restoreFocusAndPaste();
            return;
        }

        ++(*pollCount);
    });
    altReleaseTimer->start();
#else
    restoreFocusAndPaste();
#endif
}
void MPasteWidget::setVisibleWithAnnimation(bool visible) {
    if (visible == isVisible()) return;

    if (visible) {
        setWindowOpacity(0);
        show();
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

void MPasteWidget::debugKeyState() {
#ifdef Q_OS_WIN
    qDebug() << "Alt Key State:" << (GetAsyncKeyState(VK_MENU) & 0x8000)
             << "Window Focus:" << hasFocus()
             << "Window ID:" << winId()
             << "Is Visible:" << isVisible()
             << "Active Window:" << QApplication::activeWindow();
#endif
}
