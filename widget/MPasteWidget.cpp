#include <iostream>
#include <QScrollBar>
#include <QClipboard>
#include <QKeyEvent>
#include <QAudioOutput>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QWindow>
#include "utils/MPasteSettings.h"
#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "ClipboardItemWidget.h"
#include "utils/PlatformRelated.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

// QEvent::KeyPress conflicts with the KeyPress in X.h
#undef KeyPress

MPasteWidget::MPasteWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MPasteWidget),
    mimeData(nullptr)
{
    ui->setupUi(this);
    initializeWidget();
}

void MPasteWidget::initializeWidget() {

    this->initStyle();

    // 设置窗口属性
#ifdef Q_OS_WIN
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setAttribute(Qt::WA_KeyCompression, false);
#endif
    setFocusPolicy(Qt::StrongFocus);

    // 初始化动画
    this->searchHideAnim = new QPropertyAnimation(ui->searchEdit, "maximumWidth");
    this->searchHideAnim->setEndValue(0);
    this->searchHideAnim->setDuration(150);
    connect(this->searchHideAnim, &QPropertyAnimation::finished, ui->searchEdit, &QLineEdit::hide);

    this->searchShowAnim = new QPropertyAnimation(ui->searchEdit, "maximumWidth");
    this->searchShowAnim->setEndValue(200);
    this->searchShowAnim->setDuration(150);

    // 初始化关于窗口
    this->aboutWidget = new AboutWidget(this);
    this->aboutWidget->setWindowFlag(Qt::Tool);
    this->aboutWidget->setWindowTitle("MPaste About");
    this->aboutWidget->hide();

    // 初始化剪贴板窗口
    this->clipboardWidget = new ScrollItemsWidget("Clipboard", this);
    this->clipboardWidget->installEventFilter(this);
    connect(this->clipboardWidget, &ScrollItemsWidget::updateClipboard, this, &MPasteWidget::setClipboard);
    connect(this->clipboardWidget, &ScrollItemsWidget::doubleClicked, this, &MPasteWidget::hideAndPaste);
    connect(this->clipboardWidget, &ScrollItemsWidget::itemCountChanged, this, &MPasteWidget::updateItemCount);

    // 初始化数字键列表
    this->numKeyList << Qt::Key_1 << Qt::Key_2 << Qt::Key_3 << Qt::Key_4 << Qt::Key_5
                     << Qt::Key_6 << Qt::Key_7 << Qt::Key_8 << Qt::Key_9 << Qt::Key_0;

    // 初始化媒体播放器
    std::cout << "Init media player..." << std::endl;
    this->player = new QMediaPlayer(this);
    QAudioOutput *audioOutput = new QAudioOutput(this);
    this->player->setAudioOutput(audioOutput);
    this->player->setSource(QUrl("qrc:/resources/resources/sound.mp3"));
    std::cout << "Sound effect loaded finished" << std::endl;

    // 设置布局
    this->layout = new QHBoxLayout(ui->itemsWidget);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->addWidget(this->clipboardWidget);

    // 设置窗口属性
    this->setWindowFlags(Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::Tool | Qt::FramelessWindowHint);
    this->setObjectName("pasteWidget");
    this->setStyleSheet("QWidget#pasteWidget, #scrollAreaWidgetContents {background-color: #e6e5e4;}");

    // 初始化剪贴板监视器
    this->monitor = new ClipboardMonitor();
    connect(this->monitor, &ClipboardMonitor::clipboardUpdated, this, &MPasteWidget::clipboardUpdated);

    ui->searchEdit->installEventFilter(this);

    // 初始化菜单
    this->menu = new QMenu(this);
    this->menu->addAction(tr("About"), [this]() {
        // 获取当前屏幕
        QScreen *screen = QGuiApplication::primaryScreen();
        if (const QWindow *window = windowHandle())
            screen = window->screen();
        if (!screen)
            return;

        // 将窗口移动到屏幕中央
        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - this->aboutWidget->width()) / 2;
        int y = (screenGeometry.height() - this->aboutWidget->height()) / 2;
        this->aboutWidget->move(screenGeometry.x() + x, screenGeometry.y() + y);

        this->aboutWidget->show();    });
    this->menu->addAction(tr("Quit"), [this]() { qApp->exit(0); });

    connect(ui->menuButton, &QToolButton::clicked, this, [this]() {
        this->menu->popup(ui->menuButton->mapToGlobal(ui->menuButton->rect().bottomLeft()));
    });

    // 设置搜索功能
    connect(ui->searchEdit, &QLineEdit::textChanged, this, [this] (const QString &str) {
        this->currItemsWidget()->filterByKeyword(str);
    });
    connect(ui->searchButton, &QToolButton::clicked, this, [this] (bool flag) {
        this->setFocusOnSearch(flag);
    });

    // 初始化 Home & End 按钮
    connect(ui->firstButton, &QPushButton::clicked, this, [this]() {
        this->currItemsWidget()->moveToFirst();
    });
    connect(ui->lastButton, &QPushButton::clicked, this, [this]() {
        this->currItemsWidget()->moveToLast();
    });

    // 初始化系统托盘
    this->trayIcon = new QSystemTrayIcon(this);
    // this->trayIcon->setIcon(qApp->windowIcon());
    this->trayIcon->setIcon(QIcon(":/resources/resources/mpaste.svg"));

    this->trayIcon->setContextMenu(this->menu);
    this->trayIcon->show();
    connect(this->trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {  // Trigger 表示左键单击
            this->setVisibleWithAnnimation(true);
        }
    });
    this->trayIcon->showMessage("MPaste", tr("MPaste has started."), this->trayIcon->icon(), 1500);

    // 加载保存的数据
    this->loadFromSaveDir();
    this->monitor->clipboardChanged();
    this->setFocusOnSearch(false);

#ifdef _DEBUG
    // 添加调试计时器
    debugTimer = new QTimer(this);
    connect(debugTimer, &QTimer::timeout, this, &MPasteWidget::debugKeyState);
    debugTimer->start(1000);  // 每秒检查一次键盘状态
#endif
}


bool MPasteWidget::handleAltNumShortcut(QKeyEvent *event) {
    // qDebug() << "Key Press:" << event->key()
    //          << "Modifiers:" << event->modifiers()
    //          << "Native Modifiers:" << event->nativeModifiers()
    //          << "Native Virtual Key:" << event->nativeVirtualKey();

#ifdef Q_OS_WIN
    // 检查Alt键状态
    bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (altPressed) {
        // 检查数字键
        int keyOrder = -1;
        if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9) {
            keyOrder = (event->key() == Qt::Key_0) ? 9 : event->key() - Qt::Key_1;
        }

        if (keyOrder >= 0 && keyOrder <= 9) {
            qDebug() << "Alt+" << keyOrder << " detected";
            this->currItemsWidget()->selectedByShortcut(keyOrder);
            this->hideAndPaste();
            this->currItemsWidget()->cleanShortCutInfo();
            return true;
        }
    }
#else
    // Linux平台的原始实现
    if (event->modifiers() & Qt::AltModifier) {
        int keyOrder = this->numKeyList.indexOf(event->key());
        if (keyOrder >= 0 && keyOrder <= 9) {
            this->currItemsWidget()->selectedByShortcut(keyOrder);
            this->hideAndPaste();
            this->currItemsWidget()->cleanShortCutInfo();
            return true;
        }
    }
#endif

    return false;
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

void MPasteWidget::initStyle() {
    // 设置窗口透明
    setAttribute(Qt::WA_TranslucentBackground);
    ui->itemsWidget->setAttribute(Qt::WA_TranslucentBackground);

#ifdef Q_OS_WIN
    // Windows 平台使用模糊效果
    HWND hwnd = (HWND)this->winId();

    // 尝试使用 Windows 11 的新 API
    const DWORD DWMWA_SYSTEMBACKDROP_TYPE = 38; // Windows 11 的常量
    const DWORD DWMSBT_MAINWINDOW = 2;          // 磨砂效果

    HMODULE hDwmApi = LoadLibraryA("dwmapi.dll");
    if (hDwmApi) {
        typedef HRESULT (WINAPI *DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
        DwmSetWindowAttribute_t dwmSetWindowAttribute = (DwmSetWindowAttribute_t)GetProcAddress(hDwmApi, "DwmSetWindowAttribute");

        if (dwmSetWindowAttribute) {
            // 尝试设置 Windows 11 的磨砂效果
            DWORD value = DWMSBT_MAINWINDOW;
            HRESULT hr = dwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &value, sizeof(value));

            // 如果失败（可能是 Windows 10 或更早版本），尝试使用 Aero 效果
            if (FAILED(hr)) {
                const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
                BOOL value = TRUE;
                dwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));

                // 使用传统的模糊效果
                DWM_BLURBEHIND bb = {0};
                bb.dwFlags = DWM_BB_ENABLE;
                bb.fEnable = TRUE;
                bb.hRgnBlur = NULL;
                DwmEnableBlurBehindWindow(hwnd, &bb);
            }
        }

        FreeLibrary(hDwmApi);
    }
#endif

    // 修改样式表
    this->setObjectName("pasteWidget");
    this->setStyleSheet(R"(
        QWidget#pasteWidget {
            background-color: rgba(230, 229, 228, 180);
        }
        #scrollAreaWidgetContents {
            background-color: transparent;
        }
    )");
}

MPasteWidget::~MPasteWidget()
{
    delete ui;
}

bool MPasteWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel) {
        // it seems to crash with 5.11 on UOS, but work well on Deepin V20
#if (QT_VERSION >= QT_VERSION_CHECK(5,12,0))
        QCoreApplication::sendEvent(this->currItemsWidget()->horizontalScrollbar(), event);
        return true;
#endif
    } else if (event->type() == QEvent::KeyPress) {
        // make sure Alt+num still works even current focus is on the search area
        if (watched == ui->searchEdit) {
            auto keyEvent = dynamic_cast<QKeyEvent*>(event);
            if (keyEvent->modifiers() & Qt::AltModifier) {
                QGuiApplication::sendEvent(this, keyEvent);
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void MPasteWidget::clipboardUpdated(ClipboardItem nItem, int wId) {
    this->clipboardWidget->addAndSaveItem(nItem);
    this->player->play();
}

void MPasteWidget::setClipboard(const ClipboardItem &item) {
    if (this->mimeData != nullptr) {
        // 可能需要删除旧的 mimeData，但要小心处理
    }

    this->mimeData = new QMimeData();

    // 处理富文本应该是优先级最高的
    if (!item.getHtml().isEmpty()) {
        this->mimeData->setHtml(item.getHtml());
        // 同时设置纯文本作为后备
        if (!item.getText().isEmpty()) {
            this->mimeData->setText(item.getText());
        }
    }
    // 然后是图片
    else if (!item.getImage().isNull()) {
        this->mimeData->setImageData(item.getImage());
    }
    // 然后是文件URL
    else if (!item.getUrls().isEmpty()) {
        bool files = true;
        foreach (const QUrl &url, item.getUrls()) {
            if (!url.isLocalFile() || !QFileInfo::exists(url.toLocalFile())) {
                files = false;
                break;
            }
        }

        if (files) {
            QByteArray nautilus("x-special/nautilus-clipboard\n");
            QByteArray byteArray("copy\n");
            foreach (const QUrl &url, item.getUrls()) {
                byteArray.append(url.toEncoded()).append('\n');
            }
            this->mimeData->setData("x-special/gnome-copied-files", byteArray);
            nautilus.append(byteArray);
            this->mimeData->setData("COMPOUND_TEXT", nautilus);
            this->mimeData->setText(item.getText());
            this->mimeData->setData("text/plain;charset=utf-8", nautilus);
        }
        this->mimeData->setUrls(item.getUrls());
    }
    // 普通文本
    else if (!item.getText().isEmpty()) {
        this->mimeData->setText(item.getText());
    }
    // 颜色数据
    else if (item.getColor().isValid()) {
        this->mimeData->setColorData(item.getColor());
    }

    // 设置到系统剪贴板
    QGuiApplication::clipboard()->setMimeData(this->mimeData);
}

void MPasteWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    activateWindow();
    raise();
    setFocus();
}

void MPasteWidget::keyPressEvent(QKeyEvent *event) {
    if (handleAltNumShortcut(event)) {
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        if (ui->searchEdit->hasFocus()) {
            ui->searchEdit->clear();
            this->setFocusOnSearch(false);
        } else {
            this->hide();
        }
    } else if (event->key() == Qt::Key_Alt) {
        this->currItemsWidget()->setShortcutInfo();
    } else if (!ui->searchEdit->isVisible() && event->key() == Qt::Key_Left) {
        this->currItemsWidget()->focusMoveLeft();
    } else if (!ui->searchEdit->isVisible() && event->key() == Qt::Key_Right) {
        this->currItemsWidget()->focusMoveRight();
    } else if (!ui->searchEdit->isVisible() && event->key() == Qt::Key_Home) {
        this->currItemsWidget()->moveToFirst();
    } else if (!ui->searchEdit->isVisible() && event->key() == Qt::Key_End) {
        this->currItemsWidget()->moveToLast();
    } else if (event->key() == Qt::Key_Return) {
        this->currItemsWidget()->selectedByEnter();
        this->hideAndPaste();
    } else if (event->key() >= Qt::Key_Space && event->key() <= Qt::Key_AsciiTilde) {
        // 检查是否存在修饰键
        Qt::KeyboardModifiers modifiers = event->modifiers();
        if (modifiers & (Qt::AltModifier | Qt::ControlModifier)) {
            event->ignore();
            return;
        }

        // 直接设置文本而不是发送事件
        if (!ui->searchEdit->hasFocus()) {
            ui->searchEdit->setFocus();
            this->setFocusOnSearch(true);
        }

        // 获取当前文本并追加新字符
        QString currentText = ui->searchEdit->text();
        currentText += event->text();
        ui->searchEdit->setText(currentText);

        event->accept();
    }

    if (ui->searchEdit->isVisible() && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        QGuiApplication::sendEvent(ui->searchEdit, event);
        this->setFocusOnSearch(true);
    }
}

void MPasteWidget::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt) {
        // remove the shortcut information after releasing Alt key
        this->currItemsWidget()->cleanShortCutInfo();
    }

    QWidget::keyReleaseEvent(event);
}

void MPasteWidget::loadFromSaveDir() {
    this->clipboardWidget->loadFromSaveDir();
}

void MPasteWidget::setFocusOnSearch(bool flag) {
    if (flag) {
        ui->searchEdit->show();
        this->searchShowAnim->start();
        ui->searchEdit->setFocus();
    } else {
        this->searchHideAnim->start();
        ui->searchEdit->clearFocus();
        this->setFocus();
    }
}

ScrollItemsWidget *MPasteWidget::currItemsWidget() {
    return this->clipboardWidget;
}

void MPasteWidget::setVisibleWithAnnimation(bool visible) {
    if (visible == this->isVisible()) return;

    if (visible) {
        // 保持在原位置显示，但先设置透明度为0
        this->setWindowOpacity(0);
        this->show();

        // 创建动画
        QPropertyAnimation* animation = new QPropertyAnimation(this, "windowOpacity", this);
        animation->setDuration(200);  // 200毫秒的动画时长
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);

        // 动画结束后设置焦点
        connect(animation, &QPropertyAnimation::finished, this, [this, animation]() {
            animation->deleteLater();
            if (!ui->searchEdit->text().isEmpty()) {
                ui->searchEdit->setFocus();
            }

            // 尝试激活窗口
            for (int i = 0; i < 10; ++i) {
                if (PlatformRelated::currActiveWindow() == this->winId()) {
                    break;
                }
                PlatformRelated::activateWindow(this->winId());
            }
        });

        animation->start(QAbstractAnimation::DeleteWhenStopped);

    } else {
        QPropertyAnimation* animation = new QPropertyAnimation(this, "windowOpacity", this);
        animation->setDuration(HIDE_ANIMATION_TIME);  // 稍短的消失动画
        animation->setStartValue(1.0);
        animation->setEndValue(0.0);
        animation->setEasingCurve(QEasingCurve::InCubic);

        // 动画结束后隐藏窗口
        connect(animation, &QPropertyAnimation::finished, this, [this, animation]() {
            animation->deleteLater();
            this->hide();
            this->currItemsWidget()->cleanShortCutInfo();
        });

        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void MPasteWidget::updateItemCount(int itemCount) {
    ui->countArea->setText(QString::number(itemCount));
}

void MPasteWidget::hideAndPaste() {
    this->hide();

#ifdef Q_OS_WIN
    // 等待 Alt 键释放
    while (GetAsyncKeyState(VK_MENU) & 0x8000) {
        QThread::msleep(10);
    }
    // 再多等待一小段时间确保键盘状态完全恢复
    QThread::msleep(50);
#else
    QThread::usleep(100);
#endif

    if (MPasteSettings::getInst()->isAutoPaste()) {
        PlatformRelated::triggerPasteShortcut();
    }

}
