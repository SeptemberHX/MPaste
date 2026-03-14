// input: Depends on MPasteWidget.h, Qt runtime services, resource assets, and platform clipboard/window helpers.
// output: Implements the main window, item interaction flow, reliable quick-paste shortcuts, and plain-text paste behavior.
// pos: Widget-layer main window implementation coordinating boards, shortcuts, and system integration.
// update: If I change, update this header block and my folder README.md.
// note: Added theme application, dark mode propagation, and Linux frosted-tint fallback (Wayland cannot do true acrylic blur).
#include <QScrollBar>
#include <QClipboard>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QButtonGroup>
#include <QLocale>
#include <QIcon>
#include <QAction>
#include <QStringList>
#include <QTimer>
#include <QTextDocument>
#include <QWheelEvent>
#include <QCursor>
#include <QWindow>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QRandomGenerator>
#include <QStyle>
#include <QStyleFactory>
#include <cmath>
#include <algorithm>
#include <limits>
#include "utils/MPasteSettings.h"
#include "utils/ThemeManager.h"
#include "utils/IconResolver.h"
#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "utils/PlatformRelated.h"

namespace {
bool isWaylandPlatform() {
    const QString platform = QGuiApplication::platformName().toLower();
    return platform.contains(QStringLiteral("wayland"));
}

QRect availableGeometryForWidget(QScreen *screen) {
    if (!screen) {
        return {};
    }

    QRect available = screen->availableGeometry();
    if (!available.isValid()) {
        return available;
    }

    if (!isWaylandPlatform()) {
        return available;
    }

    const qreal dpr = screen->devicePixelRatio();
    if (dpr <= 1.0) {
        return available;
    }

    return QRect(qRound(available.x() / dpr),
                 qRound(available.y() / dpr),
                 qRound(available.width() / dpr),
                 qRound(available.height() / dpr));
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

#ifndef Q_OS_WIN
QAudioFormat chooseCopySoundFormat(const QAudioDevice &device) {
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isNull() && device.isFormatSupported(format)) {
        return format;
    }

    format.setChannelCount(2);
    if (!device.isNull() && device.isFormatSupported(format)) {
        return format;
    }

    format.setSampleRate(48000);
    if (!device.isNull() && device.isFormatSupported(format)) {
        return format;
    }

    if (!device.isNull()) {
        QAudioFormat preferred = device.preferredFormat();
        if (preferred.isValid()) {
            if (device.isFormatSupported(preferred)) {
                return preferred;
            }
            if (device.supportedSampleFormats().contains(QAudioFormat::Int16)) {
                QAudioFormat candidate = preferred;
                candidate.setSampleFormat(QAudioFormat::Int16);
                if (device.isFormatSupported(candidate)) {
                    return candidate;
                }
            }
        }
    }

    return format;
}

QByteArray buildCopySoundPcm(const QAudioFormat &format) {
    if (!format.isValid()) {
        return {};
    }

    constexpr int durationMs = 70;
    constexpr double freqA = 880.0;
    constexpr double freqB = 1320.0;
    constexpr double amplitude = 0.28;

    const int sampleRate = format.sampleRate();
    const int channels = format.channelCount();
    const int frames = qMax(1, sampleRate * durationMs / 1000);
    const int bytesPerFrame = format.bytesPerFrame();
    if (channels <= 0 || bytesPerFrame <= 0) {
        return {};
    }

    QByteArray out(frames * bytesPerFrame, Qt::Uninitialized);

    const double invFrames = frames > 1 ? 1.0 / static_cast<double>(frames - 1) : 1.0;
    const double invSampleRate = 1.0 / static_cast<double>(sampleRate);
    constexpr double pi = 3.14159265358979323846;
    constexpr double twoPi = 2.0 * pi;

    auto envelope = [&](int frameIndex) {
        const double x = frameIndex * invFrames; // 0..1
        const double hann = 0.5 * (1.0 - std::cos(twoPi * x));
        const double t = frameIndex * invSampleRate;
        const double decay = std::exp(-t * 18.0);
        return hann * decay;
    };

    auto sampleAt = [&](int frameIndex) {
        const double t = frameIndex * invSampleRate;
        const double tone = std::sin(twoPi * freqA * t) * 0.68 + std::sin(twoPi * freqB * t) * 0.32;
        double v = tone * envelope(frameIndex) * amplitude;
        v = std::clamp(v, -1.0, 1.0);
        return v;
    };

    const auto writeSample = [&](char *dst, double v) {
        switch (format.sampleFormat()) {
            case QAudioFormat::UInt8: {
                const int value = qRound((v * 0.5 + 0.5) * 255.0);
                *reinterpret_cast<quint8 *>(dst) = static_cast<quint8>(qBound(0, value, 255));
                break;
            }
            case QAudioFormat::Int16: {
                const int value = qRound(v * 32767.0);
                *reinterpret_cast<qint16 *>(dst) = static_cast<qint16>(qBound(-32768, value, 32767));
                break;
            }
            case QAudioFormat::Int32: {
                const qint64 value = qRound64(v * 2147483647.0);
                const qint64 clamped = std::clamp<qint64>(value, std::numeric_limits<qint32>::min(), std::numeric_limits<qint32>::max());
                *reinterpret_cast<qint32 *>(dst) = static_cast<qint32>(clamped);
                break;
            }
            case QAudioFormat::Float: {
                *reinterpret_cast<float *>(dst) = static_cast<float>(v);
                break;
            }
            case QAudioFormat::Unknown:
            case QAudioFormat::NSampleFormats:
            default:
                break;
        }
    };

    char *raw = out.data();
    const int bytesPerSample = format.bytesPerSample();
    for (int i = 0; i < frames; ++i) {
        const double v = sampleAt(i);
        for (int ch = 0; ch < channels; ++ch) {
            writeSample(raw + (i * bytesPerFrame) + (ch * bytesPerSample), v);
        }
    }

    return out;
}
#endif

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
    loadFromSaveDir();
    qInfo().noquote() << QStringLiteral("[startup] loadFromSaveDir done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    clipboard_.monitor->primeCurrentClipboard();
    qInfo().noquote() << QStringLiteral("[startup] primeCurrentClipboard done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());

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

    ui_.aboutWidget = new AboutWidget(this);
    ui_.aboutWidget->setWindowFlag(Qt::Tool);
    ui_.aboutWidget->setWindowTitle("MPaste About");
    ui_.aboutWidget->hide();

    ui_.detailsDialog = new ClipboardItemDetailsDialog(this);
    ui_.detailsDialog->setWindowFlag(Qt::Tool);
    ui_.detailsDialog->hide();

    ui_.previewDialog = new ClipboardItemPreviewDialog(this);
    ui_.previewDialog->setWindowFlag(Qt::Tool);
    ui_.previewDialog->hide();

    ui_.settingsWidget = new MPasteSettingsWidget(this);
    connect(ui_.settingsWidget, &MPasteSettingsWidget::shortcutChanged,
            this, &MPasteWidget::shortcutChanged);
    connect(ui_.settingsWidget, &MPasteSettingsWidget::historyRetentionChanged,
            this, &MPasteWidget::reloadHistoryBoards);

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

    ui_.ui->countArea->setToolButtonStyle(Qt::ToolButtonTextOnly);

    // Adjust window height to fit content (adapts to card scale)
    adjustSize();
    if (waylandDockHeight_ <= 0) {
        waylandDockHeight_ = height();
    }

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
    ui_.ui->richTextTypeBtn->setProperty("contentType", static_cast<int>(ClipboardItem::RichText));
    ui_.ui->fileTypeBtn->setProperty("contentType", static_cast<int>(ClipboardItem::File));

    ui_.typeButtonGroup->addButton(ui_.ui->allTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->textTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->linkTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->imageTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->richTextTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->fileTypeBtn);

    ui_.ui->allTypeBtn->setChecked(true);

    ui_.ui->clipboardButton->setChecked(true);

    initMenu();

    ui_.ui->searchEdit->installEventFilter(this);
    ui_.ui->clipboardBtnWidget->installEventFilter(this);
    ui_.ui->typeBtnWidget->installEventFilter(this);

    applyTheme(ThemeManager::instance()->isDark());

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
#ifdef Q_OS_WIN
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
#else
    if (misc_.audioSink) {
        misc_.audioSink->stop();
        delete misc_.audioSink;
        misc_.audioSink = nullptr;
    }
    if (misc_.audioBuffer) {
        misc_.audioBuffer->close();
        delete misc_.audioBuffer;
        misc_.audioBuffer = nullptr;
    }

    misc_.audioDevice = device;
    misc_.audioFormat = chooseCopySoundFormat(device);
    misc_.audioPcm = buildCopySoundPcm(misc_.audioFormat);
    if (misc_.audioPcm.isEmpty() || !misc_.audioFormat.isValid()) {
        qWarning() << "[clipboard-widget] copy sound disabled: unsupported audio format" << misc_.audioFormat;
        return;
    }

    misc_.audioBuffer = new QBuffer(this);
    misc_.audioBuffer->setData(misc_.audioPcm);
    misc_.audioBuffer->open(QIODevice::ReadOnly);

    misc_.audioSink = new QAudioSink(device, misc_.audioFormat, this);
    misc_.audioSink->setVolume(1.0);
#endif
}

void MPasteWidget::syncSoundOutputDevice() {
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
#ifdef Q_OS_WIN
    if (misc_.player && misc_.audioOutput
        && misc_.audioOutput->device().id() == defaultDevice.id()) {
        return;
    }
    rebuildSoundPlaybackChain(defaultDevice);
#else
    if (misc_.audioSink && !misc_.audioDevice.isNull()
        && misc_.audioDevice.id() == defaultDevice.id()) {
        return;
    }
    rebuildSoundPlaybackChain(defaultDevice);
#endif
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
        QScreen *screen = QGuiApplication::primaryScreen();
        if (const QWindow *window = windowHandle())
            screen = window->screen();
        if (!screen)
            return;

        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - ui_.aboutWidget->width()) / 2;
        int y = (screenGeometry.height() - ui_.aboutWidget->height()) / 2;
        ui_.aboutWidget->move(screenGeometry.x() + x, screenGeometry.y() + y);
        ui_.aboutWidget->show();
    });

    ui_.settingsAction = new QAction(
        IconResolver::themedIcon(QStringLiteral("settings"), ThemeManager::instance()->isDark()),
        menuText("Settings", QString::fromUtf16(u"\u8BBE\u7F6E")),
        this);
    connect(ui_.settingsAction, &QAction::triggered, this, [this]() {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (const QWindow *window = windowHandle())
            screen = window->screen();
        if (!screen)
            return;

        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - ui_.aboutWidget->width()) / 2;
        int y = (screenGeometry.height() - ui_.aboutWidget->height()) / 2;
        ui_.settingsWidget->move(screenGeometry.x() + x, screenGeometry.y() + y);
        ui_.settingsWidget->show();
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
    applyMenuTheme(ui_.menu);
    update();
}

void MPasteWidget::setupConnections() {
    connect(clipboard_.monitor, &ClipboardMonitor::clipboardActivityObserved,
            this, &MPasteWidget::clipboardActivityObserved);
    connect(clipboard_.monitor, &ClipboardMonitor::clipboardUpdated,
            this, &MPasteWidget::clipboardUpdated);
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
            ui_.detailsDialog->showItem(item, sequence, totalCount);
        });
        connect(boardWidget, &ScrollItemsWidget::previewRequested,
        this, [this](const ClipboardItem &item) {
            if (ClipboardItemPreviewDialog::supportsPreview(item)) {
                ui_.previewDialog->showItem(item);
            }
        });

        connect(boardWidget, &ScrollItemsWidget::itemCountChanged, this, [this, boardWidget](int itemCount) {
            if (ui_.buttonGroup->checkedButton()->property("category").toString() == boardWidget->getCategory()) {
                this->updateItemCount(itemCount);
            }
        });

        connect(boardWidget, &ScrollItemsWidget::itemStared, this, [this](const ClipboardItem &item) {
            ClipboardItem updatedItem(item);
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
#ifdef Q_OS_WIN
    if (misc_.player) {
        if (misc_.player->mediaStatus() == QMediaPlayer::EndOfMedia) {
            misc_.player->setPosition(0);
        }
        if (misc_.player->playbackState() == QMediaPlayer::PlayingState) {
            misc_.player->stop();
        }
        misc_.player->play();
    }
#else
    if (misc_.audioSink && misc_.audioBuffer) {
        if (misc_.audioSink->state() == QAudio::ActiveState) {
            misc_.audioSink->stop();
        }
        misc_.audioBuffer->seek(0);
        misc_.audioSink->start(misc_.audioBuffer);
    }
#endif
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

void MPasteWidget::clipboardUpdated(ClipboardItem nItem, int wId) {
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

    QMimeData *mimeData = plainText ? createPlainTextMimeData(item) : item.createMimeData();
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
    const ClipboardItem *selectedItem = currItemsWidget()->selectedByEnter();
    if (selectedItem && setClipboard(*selectedItem, plainText)) {
        hideAndPaste();
    }
}

void MPasteWidget::handlePreviewKey() {
    if (ui_.previewDialog && ui_.previewDialog->isVisible()) {
        ui_.previewDialog->reject();
        return;
    }

    const ClipboardItem *selectedItem = currItemsWidget()->currentSelectedItem();
    if (!selectedItem || !ClipboardItemPreviewDialog::supportsPreview(*selectedItem)) {
        return;
    }

    ui_.previewDialog->showItem(*selectedItem);
}

bool MPasteWidget::triggerShortcutPaste(int shortcutIndex, bool plainText) {
    if (shortcutIndex < 0 || shortcutIndex > 9) {
        return false;
    }

    const ClipboardItem *selectedItem = currItemsWidget()->selectedByShortcut(shortcutIndex);
    if (!selectedItem || !setClipboard(*selectedItem, plainText)) {
        return false;
    }

    QTimer::singleShot(50, this, [this]() {
        hideAndPaste();
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
    if (waylandDockEnabled_) {
        updateWaylandDockMask();
    }
}

void MPasteWidget::mousePressEvent(QMouseEvent *event) {
    if (waylandDockEnabled_ && event && !waylandDockRect().contains(event->pos())) {
        hide();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MPasteWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal bw = 3.0;
    const qreal radius = 10.0;
    const QRectF baseRect = waylandDockEnabled_ ? QRectF(waylandDockRect()) : QRectF(rect());
    QRectF r = baseRect.adjusted(bw / 2.0, bw / 2.0, -bw / 2.0, -bw / 2.0);

    QPainterPath path;
    path.addRoundedRect(r, radius, radius);

    // Clear to transparent first so DWM glass/acrylic shows through
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

#ifndef Q_OS_WIN
    // Wayland does not allow sampling the screen behind the window, so we can't implement a true acrylic blur.
    // Instead, paint a frosted-tint background that stays readable and visually similar to the Windows acrylic style.
    {
        p.save();
        p.setClipPath(path);

        QLinearGradient bgGrad(r.topLeft(), r.bottomRight());
        if (darkTheme_) {
            bgGrad.setColorAt(0.0, QColor(24, 29, 36, 230));
            bgGrad.setColorAt(1.0, QColor(32, 40, 50, 210));
        } else {
            bgGrad.setColorAt(0.0, QColor(250, 252, 255, 235));
            bgGrad.setColorAt(1.0, QColor(235, 242, 248, 220));
        }
        p.fillPath(path, bgGrad);

        static QPixmap noiseLight;
        static qreal noiseLightDpr = 0.0;
        static QPixmap noiseDark;
        static qreal noiseDarkDpr = 0.0;
        const qreal dpr = devicePixelRatioF();
        QPixmap &noisePixmap = darkTheme_ ? noiseDark : noiseLight;
        qreal &noiseDpr = darkTheme_ ? noiseDarkDpr : noiseLightDpr;
        if (noisePixmap.isNull() || !qFuzzyCompare(noiseDpr, dpr)) {
            noiseDpr = dpr;
            constexpr int logicalSize = 128;
            const int pixelSize = qMax(16, qRound(logicalSize * qMax<qreal>(1.0, dpr)));
            QImage img(pixelSize, pixelSize, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);

            const quint32 seed = darkTheme_ ? 0xC0FFEEu : 0xBADC0DEu;
            QRandomGenerator rng(seed);
            for (int y = 0; y < img.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    const int alphaBase = darkTheme_ ? 16 : 12;
                    const int alpha = alphaBase + rng.bounded(darkTheme_ ? 12 : 10);
                    const int val = darkTheme_ ? (235 + rng.bounded(21)) : rng.bounded(26);
                    line[x] = qRgba(val, val, val, alpha);
                }
            }

            noisePixmap = QPixmap::fromImage(img);
            noisePixmap.setDevicePixelRatio(qMax<qreal>(1.0, dpr));
        }

        p.setOpacity(darkTheme_ ? 0.08 : 0.06);
        p.drawTiledPixmap(baseRect.toRect(), noisePixmap);
        p.setOpacity(1.0);

        // Top sheen (subtle highlight)
        QLinearGradient sheen(r.topLeft(), r.bottomLeft());
        if (darkTheme_) {
            sheen.setColorAt(0.0, QColor(255, 255, 255, 18));
            sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        } else {
            sheen.setColorAt(0.0, QColor(255, 255, 255, 80));
            sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        }
        const QRectF sheenRect(r.left(), r.top(), r.width(), r.height() * 0.46);
        p.fillRect(sheenRect, sheen);

        p.restore();
    }
#endif

    // Gradient border
    QConicalGradient grad(r.center(), 135);
    grad.setColorAt(0.00, QColor("#4A90E2"));
    grad.setColorAt(0.25, QColor("#1abc9c"));
    grad.setColorAt(0.50, QColor("#fc9867"));
    grad.setColorAt(0.75, QColor("#9B59B6"));
    grad.setColorAt(1.00, QColor("#4A90E2"));

    p.setPen(QPen(QBrush(grad), bw));
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
    if (waylandDockEnabled_) {
        updateWaylandDockMask();
    }
}

void MPasteWidget::prepareWaylandDock(const QRect &availableGeometry) {
    if (!isWaylandPlatform() || !availableGeometry.isValid()) {
        waylandDockEnabled_ = false;
        clearMask();
        return;
    }

    waylandDockEnabled_ = true;

    if (waylandDockHeight_ <= 0) {
        waylandDockHeight_ = qMax(1, sizeHint().height());
    }

    if (!waylandStretchInserted_ && ui_.ui && ui_.ui->verticalLayout) {
        ui_.ui->verticalLayout->insertStretch(0, 1);
        waylandStretchInserted_ = true;
    }

    // GNOME Wayland does not honor client-side move() for toplevels.
    // Resize the surface to the available screen size, and keep the actual visible/input region
    // as a bottom dock via mask + top stretch.
    setFixedWidth(availableGeometry.width());
    setFixedHeight(availableGeometry.height());
    resize(availableGeometry.size());

    updateWaylandDockMask();
}

QRect MPasteWidget::waylandDockRect() const {
    if (!waylandDockEnabled_) {
        return rect();
    }

    const int dockHeight = qBound(1, waylandDockHeight_, height());
    return QRect(0, height() - dockHeight, width(), dockHeight);
}

void MPasteWidget::updateWaylandDockMask() {
    if (!waylandDockEnabled_) {
        clearMask();
        return;
    }

    setMask(QRegion(waylandDockRect()));
    update();
}

void MPasteWidget::loadFromSaveDir() {
    ui_.staredWidget->loadFromSaveDir();
    for (const ClipboardItem &item : ui_.staredWidget->allItems()) {
        ui_.clipboardWidget->setItemFavorite(item, true);
    }
    ui_.clipboardWidget->loadFromSaveDirDeferred();
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
    QAbstractButton* currentBtn = ui_.buttonGroup->checkedButton();
    if (currentBtn) {
        QString category = currentBtn->property("category").toString();
        return ui_.boardWidgetMap[category];
    }

    return ui_.clipboardWidget;
}

void MPasteWidget::updateItemCount(int itemCount) {
    ui_.ui->countArea->setText(QString::number(itemCount));
    ui_.ui->countArea->adjustSize();
    ui_.ui->countArea->setFixedWidth(qMax(30, ui_.ui->countArea->sizeHint().width()));
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

    const bool wayland = isWaylandPlatform();

    if (visible) {
        if (wayland) {
            QScreen *screen = nullptr;
            if (QWindow *handle = windowHandle()) {
                screen = handle->screen();
            }
            if (!screen) {
                screen = QGuiApplication::screenAt(QCursor::pos());
            }
            if (!screen) {
                screen = QGuiApplication::primaryScreen();
            }
            prepareWaylandDock(availableGeometryForWidget(screen));

            show();
            if (clipboard_.copiedWhenHide) {
                ui_.clipboardWidget->scrollToFirst();
                clipboard_.copiedWhenHide = false;
            }
            raise();
            activateWindow();
            setFocus();
            return;
        }

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
        if (wayland) {
            hide();
            currItemsWidget()->cleanShortCutInfo();
            return;
        }

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
