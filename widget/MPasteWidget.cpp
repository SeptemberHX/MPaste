// input: Depends on MPasteWidget.h, Qt runtime services, resource assets, and platform clipboard/window helpers.
// output: Implements the main window, item interaction flow, reliable quick-paste shortcuts, and plain-text paste behavior.
// pos: Widget-layer main window implementation coordinating boards, shortcuts, and system integration.
// update: If I change, update this header block and my folder README.md.
#include <iostream>
#include <QScrollBar>
#include <QClipboard>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QAudioOutput>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QButtonGroup>
#include <QStringList>
#include <QTimer>
#include <QTextDocument>
#include <QWheelEvent>
#include <QWindow>
#include <QPainter>
#include <QPainterPath>
#include "utils/MPasteSettings.h"
#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "ClipboardItemWidget.h"
#include "utils/PlatformRelated.h"

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

static void enableBlurBehind(HWND hwnd) {
    // Extend DWM frame — needed for glass composition
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    // Native rounded corners (Windows 11) — DWMWA_WINDOW_CORNER_PREFERENCE = 33
    DWORD preference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));

    // Acrylic blur via SetWindowCompositionAttribute (instant, no flicker)
    auto user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto setWCA = reinterpret_cast<pfnSetWindowCompositionAttribute>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!setWCA) return;

    // GradientColor format: AABBGGRR — light tint applied by DWM itself
    DWORD tint = (96u << 24) | (249u << 16) | (246u << 8) | 235u; // rgba(235,246,249,96)

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
    initStyle();
    initUI();
    initClipboard();
    initShortcuts();
    initSystemTray();
    initSound();
    setupConnections();

    loadFromSaveDir();
    clipboard_.monitor->clipboardChanged();
    setFocusOnSearch(false);
    misc_.pendingNumKey = 0;  // 初始化待处理的数字键

#ifdef _DEBUG
    // 添加调试计时器
    QTimer* debugTimer = new QTimer(this);
    connect(debugTimer, &QTimer::timeout, this, &MPasteWidget::debugKeyState);
    debugTimer->start(1000);  // 每秒检查一次键盘状态
#endif
}

void MPasteWidget::initStyle() {
    // 保持原有的基础窗口标志
    Qt::WindowFlags flags = Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::Tool | Qt::FramelessWindowHint;

#ifdef Q_OS_LINUX
    // Linux 特定的窗口标志和属性
    flags |= Qt::X11BypassWindowManagerHint;  // 绕过窗口管理器的一些处理
    flags |= Qt::Window;  // 确保窗口可以覆盖其他窗口
#endif
    setWindowFlags(flags);

    setFocusPolicy(Qt::StrongFocus);

#ifdef Q_OS_WIN
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setAttribute(Qt::WA_KeyCompression, false);
    setAttribute(Qt::WA_NoSystemBackground);
    enableBlurBehind((HWND)winId());
#else
    setAttribute(Qt::WA_TranslucentBackground);
#endif

    setAttribute(Qt::WA_AlwaysStackOnTop);
    ui_.ui->itemsWidget->setAttribute(Qt::WA_TranslucentBackground);
    ui_.ui->itemsWidget->setAttribute(Qt::WA_NoSystemBackground, false);

    setObjectName("pasteWidget");
    QFile styleFile(":/resources/resources/style/defaultStyle.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QString style = QLatin1String(styleFile.readAll());
        setStyleSheet(style);
        styleFile.close();
    } else {
        qWarning() << "Failed to load style sheet:" << styleFile.errorString();
    }
}

void MPasteWidget::initUI() {
    // 初始化搜索动画
    initSearchAnimations();

    // 初始化关于窗口
    ui_.aboutWidget = new AboutWidget(this);
    ui_.aboutWidget->setWindowFlag(Qt::Tool);
    ui_.aboutWidget->setWindowTitle("MPaste About");
    ui_.aboutWidget->hide();

    ui_.settingsWidget = new MPasteSettingsWidget(this);
    connect(ui_.settingsWidget, &MPasteSettingsWidget::shortcutChanged,
            this, &MPasteWidget::shortcutChanged);

    // 初始化剪贴板窗口
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

    // 设置布局
    ui_.layout = new QHBoxLayout(ui_.ui->itemsWidget);
    ui_.layout->setContentsMargins(0, 0, 0, 0);
    ui_.layout->setSpacing(0);
    ui_.layout->addWidget(ui_.clipboardWidget);
    ui_.layout->addWidget(ui_.staredWidget);

    ui_.staredWidget->hide();

    // Adjust window height to fit content (adapts to card scale)
    adjustSize();

    // 初始化 buttonGroup
    ui_.buttonGroup = new QButtonGroup(this);
    ui_.buttonGroup->setExclusive(true);
    ui_.buttonGroup->addButton(ui_.ui->clipboardButton);
    ui_.buttonGroup->addButton(ui_.ui->staredButton);

    // 初始化类型过滤 buttonGroup
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

    // 默认显示
    ui_.ui->clipboardButton->setChecked(true);

    // 初始化菜单
    initMenu();

    ui_.ui->searchEdit->installEventFilter(this);
    ui_.ui->clipboardBtnWidget->installEventFilter(this);
    ui_.ui->typeBtnWidget->installEventFilter(this);
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
    std::cout << "Init media player..." << std::endl;
    misc_.player = new QMediaPlayer(this);
    QAudioOutput *audioOutput = new QAudioOutput(this);
    misc_.player->setAudioOutput(audioOutput);
    misc_.player->setSource(QUrl("qrc:/resources/resources/sound.mp3"));
    std::cout << "Sound effect loaded finished" << std::endl;
}

void MPasteWidget::initSystemTray() {
    ui_.trayIcon = new QSystemTrayIcon(this);
    ui_.trayIcon->setIcon(QIcon(":/resources/resources/mpaste.svg"));
    ui_.trayIcon->setContextMenu(ui_.menu);
    ui_.trayIcon->show();
}

void MPasteWidget::initMenu() {
    ui_.menu = new QMenu(this);
    ui_.menu->addAction(tr("About"), [this]() {
        // 获取当前屏幕
        QScreen *screen = QGuiApplication::primaryScreen();
        if (const QWindow *window = windowHandle())
            screen = window->screen();
        if (!screen)
            return;

        // 将窗口移动到屏幕中央
        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - ui_.aboutWidget->width()) / 2;
        int y = (screenGeometry.height() - ui_.aboutWidget->height()) / 2;
        ui_.aboutWidget->move(screenGeometry.x() + x, screenGeometry.y() + y);

        ui_.aboutWidget->show();
    });
    ui_.menu->addAction(tr("Settings"), [this]() {
        // 获取当前屏幕
        QScreen *screen = QGuiApplication::primaryScreen();
        if (const QWindow *window = windowHandle())
            screen = window->screen();
        if (!screen)
            return;

        // 将窗口移动到屏幕中央
        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - ui_.aboutWidget->width()) / 2;
        int y = (screenGeometry.height() - ui_.aboutWidget->height()) / 2;
        ui_.settingsWidget->move(screenGeometry.x() + x, screenGeometry.y() + y);
        ui_.settingsWidget->show();
    });
    ui_.menu->addAction(tr("Quit"), [this]() { qApp->exit(0); });
}

void MPasteWidget::setupConnections() {
    // 剪贴板连接
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

        connect(boardWidget, &ScrollItemsWidget::itemCountChanged, this, [this, boardWidget](int itemCount) {
            if (ui_.buttonGroup->checkedButton()->property("category").toString() == boardWidget->getCategory()) {
                this->updateItemCount(itemCount);
            }
        });

        connect(boardWidget, &ScrollItemsWidget::itemStared, this, [this](const ClipboardItem &item) {
            ClipboardItem updatedItem(item);
            ui_.staredWidget->addAndSaveItem(updatedItem);
        });
        connect(boardWidget, &ScrollItemsWidget::itemUnstared, this, [this, boardWidget](const ClipboardItem &item) {
            ui_.staredWidget->removeItemByContent(item);
            // Sync: un-star in other boards too
            if (boardWidget == ui_.staredWidget) {
                ui_.clipboardWidget->setItemFavorite(item, false);
            }
        });
    }

    // 菜单按钮连接
    connect(ui_.ui->menuButton, &QToolButton::clicked, this, [this]() {
        ui_.menu->popup(ui_.ui->menuButton->mapToGlobal(ui_.ui->menuButton->rect().bottomLeft()));
    });

    // 搜索功能连接
    connect(ui_.ui->searchEdit, &QLineEdit::textChanged, this, [this](const QString &str) {
        this->currItemsWidget()->filterByKeyword(str);
    });
    connect(ui_.ui->searchButton, &QToolButton::clicked, this, [this](bool flag) {
        this->setFocusOnSearch(flag);
    });

    // Home & End 按钮连接
    connect(ui_.ui->firstButton, &QPushButton::clicked, this, [this]() {
        this->currItemsWidget()->scrollToFirst();
    });
    connect(ui_.ui->lastButton, &QPushButton::clicked, this, [this]() {
        this->currItemsWidget()->scrollToLast();
    });

    // 系统托盘连接
    connect(ui_.trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            this->setVisibleWithAnnimation(true);
        }
    });

    // 类型过滤按钮连接
    connect(ui_.typeButtonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
        [this](QAbstractButton *button) {
            auto type = static_cast<ClipboardItem::ContentType>(button->property("contentType").toInt());
            this->currItemsWidget()->filterByType(type);
        });

    // 处理切换事件
    connect(ui_.buttonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
        [this](QAbstractButton *button) {
            // 获取当前类型过滤
            auto typeBtn = ui_.typeButtonGroup->checkedButton();
            auto type = typeBtn ? static_cast<ClipboardItem::ContentType>(typeBtn->property("contentType").toInt())
                                : ClipboardItem::All;

            // 处理切换逻辑
            for (auto *toolButton : ui_.buttonGroup->buttons()) {
                auto *boardWidget = ui_.boardWidgetMap[toolButton->property("category").toString()];
                boardWidget->setVisible(toolButton == button);
                if (toolButton == button) {
                    // 设置过滤器并统一应用（filterByKeyword 内部会调用 applyFilters）
                    boardWidget->filterByType(type);
                    boardWidget->filterByKeyword(ui_.ui->searchEdit->text());
                }
            }
        });
}

void MPasteWidget::clipboardUpdated(ClipboardItem nItem, int wId) {
    if (!clipboard_.isPasting) {
        ui_.clipboardWidget->addAndSaveItem(nItem);

        if (MPasteSettings::getInst()->isPlaySound()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (misc_.player->playbackState() != QMediaPlayer::PlayingState) {
                if (misc_.player->mediaStatus() == QMediaPlayer::EndOfMedia) {
                    misc_.player->setPosition(0);
                }
                misc_.player->play();
                misc_.lastSoundPlayAtMs = now;
            } else if (now - misc_.lastSoundPlayAtMs > 250) {
                misc_.player->stop();
                misc_.player->play();
                misc_.lastSoundPlayAtMs = now;
            }
        }

        clipboard_.copiedWhenHide = true;
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
    clipboard_.monitor->disconnectMonitor();

    QMimeData *mimeData = plainText ? createPlainTextMimeData(item) : item.createMimeData();
    if (!mimeData) {
        clipboard_.monitor->connectMonitor();
        return false;
    }

    if (!plainText && item.getContentType() == ClipboardItem::File) {
        handleUrlsClipboard(mimeData, item);
    }

    QGuiApplication::clipboard()->setMimeData(mimeData);
    QTimer::singleShot(200, this, [this]() {
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
    // 切换到下一个 tab 按钮
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

// 事件处理相关
bool MPasteWidget::eventFilter(QObject *watched, QEvent *event) {
    // 为药丸容器绘制炫彩渐变边框
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
        // 不拦截，让子控件继续绘制
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

    // All 4 corners rounded — matches DWM native DWMWCP_ROUND clipping
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
    p.setBrush(QColor(245, 248, 250, 14));
    p.drawPath(path);
}

void MPasteWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    activateWindow();
    raise();
    setFocus();
}

void MPasteWidget::loadFromSaveDir() {
    std::cout << QDateTime::currentDateTime().toString().toStdString() << "Loading items for boards" << std::endl;
    for (auto *boardWidget : ui_.boardWidgetMap.values()) {
        boardWidget->loadFromSaveDir();
    }
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
}

void MPasteWidget::hideAndPaste() {
    WId previousWId = PlatformRelated::previousActiveWindow();

    hide();

    if (!MPasteSettings::getInst()->isAutoPaste()) {
        return;
    }

    clipboard_.isPasting = true;

    auto finishPaste = [this]() {
        PlatformRelated::triggerPasteShortcut();
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
