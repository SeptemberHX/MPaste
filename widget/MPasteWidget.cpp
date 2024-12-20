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
    QWidget(parent)
{
    ui_.ui = new Ui::MPasteWidget;
    ui_.ui->setupUi(this);
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

#ifdef _DEBUG
    // 添加调试计时器
    QTimer* debugTimer = new QTimer(this);
    connect(debugTimer, &QTimer::timeout, this, &MPasteWidget::debugKeyState);
    debugTimer->start(1000);  // 每秒检查一次键盘状态
#endif
}

void MPasteWidget::initStyle() {
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::Tool | Qt::FramelessWindowHint);
    setFocusPolicy(Qt::StrongFocus);

#ifdef Q_OS_WIN
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setAttribute(Qt::WA_KeyCompression, false);
#endif

    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground, false);
    ui_.ui->itemsWidget->setAttribute(Qt::WA_TranslucentBackground);
    ui_.ui->itemsWidget->setAttribute(Qt::WA_NoSystemBackground, false);

    setObjectName("pasteWidget");
    setStyleSheet(R"(
        QWidget#pasteWidget {
            background: qlineargradient(
                x1: 0, y1: 0,
                x2: 1, y2: 1,
                stop: 0 rgba(235, 246, 249, 255),    /* 浅蓝白色 */
                stop: 0.5 rgba(242, 245, 236, 255),  /* 浅绿白色 */
                stop: 1 rgba(235, 246, 249, 255)     /* 浅蓝白色 */
            );
            border: 1px solid black;
            border-radius: 8px;  /* 添加圆角 */
        }
        #scrollAreaWidgetContents {
            background-color: transparent;
        }
        #itemsWidget {
            background-color: transparent;
            border: none;
        }
        QScrollArea {
            background-color: transparent;
        }
    )");
}

void MPasteWidget::initUI() {
    // 初始化搜索动画
    initSearchAnimations();

    // 初始化关于窗口
    ui_.aboutWidget = new AboutWidget(this);
    ui_.aboutWidget->setWindowFlag(Qt::Tool);
    ui_.aboutWidget->setWindowTitle("MPaste About");
    ui_.aboutWidget->hide();

    // 初始化剪贴板窗口
    ui_.clipboardWidget = new ScrollItemsWidget("Clipboard", this);
    ui_.clipboardWidget->installEventFilter(this);

    // 设置布局
    ui_.layout = new QHBoxLayout(ui_.ui->itemsWidget);
    ui_.layout->setContentsMargins(0, 0, 0, 0);
    ui_.layout->addWidget(ui_.clipboardWidget);

    // 初始化菜单
    initMenu();

    ui_.ui->searchEdit->installEventFilter(this);
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
    clipboard_.mimeData = nullptr;
    clipboard_.isPasting = false;
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
    ui_.menu->addAction(tr("Quit"), [this]() { qApp->exit(0); });
}

void MPasteWidget::setupConnections() {
    // 剪贴板连接
    connect(clipboard_.monitor, &ClipboardMonitor::clipboardUpdated,
            this, &MPasteWidget::clipboardUpdated);
    connect(ui_.clipboardWidget, &ScrollItemsWidget::doubleClicked,
            this, [this](const ClipboardItem &item) {
                this->setClipboard(item);
                this->hideAndPaste();
            });
    connect(ui_.clipboardWidget, &ScrollItemsWidget::itemCountChanged,
            this, &MPasteWidget::updateItemCount);

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
        this->currItemsWidget()->moveToFirst();
    });
    connect(ui_.ui->lastButton, &QPushButton::clicked, this, [this]() {
        this->currItemsWidget()->moveToLast();
    });

    // 系统托盘连接
    connect(ui_.trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            this->setVisibleWithAnnimation(true);
        }
    });
}

void MPasteWidget::clipboardUpdated(ClipboardItem nItem, int wId) {
    if (!clipboard_.isPasting) {
        ui_.clipboardWidget->addAndSaveItem(nItem);
        misc_.player->play();
    }
}

void MPasteWidget::setClipboard(const ClipboardItem &item) {
    clipboard_.monitor->disconnectMonitor();

    if (clipboard_.mimeData) {

    }
    clipboard_.mimeData = new QMimeData();

    // 处理富文本
    if (!item.getHtml().isEmpty()) {
        clipboard_.mimeData->setHtml(item.getHtml());
        if (!item.getText().isEmpty()) {
            clipboard_.mimeData->setText(item.getText());
        }
    }
    // 处理图片
    else if (!item.getImage().isNull()) {
        clipboard_.mimeData->setImageData(item.getImage());
    }
    // 处理文件URL
    else if (!item.getUrls().isEmpty()) {
        handleUrlsClipboard(item);
    }
    // 处理普通文本
    else if (!item.getText().isEmpty()) {
        clipboard_.mimeData->setText(item.getText());
    }
    // 处理颜色数据
    else if (item.getColor().isValid()) {
        clipboard_.mimeData->setColorData(item.getColor());
    }

    QGuiApplication::clipboard()->setMimeData(clipboard_.mimeData);
    // 使用延时重新连接监视器，避免触发重复提示音
    QTimer::singleShot(200, this, [this]() {
        clipboard_.monitor->connectMonitor();
        delete clipboard_.mimeData;
    });
}

void MPasteWidget::handleUrlsClipboard(const ClipboardItem &item) {
    bool files = true;
    for (const QUrl &url : item.getUrls()) {
        if (!url.isLocalFile() || !QFileInfo::exists(url.toLocalFile())) {
            files = false;
            break;
        }
    }

    if (files) {
        QByteArray nautilus("x-special/nautilus-clipboard\n");
        QByteArray byteArray("copy\n");
        for (const QUrl &url : item.getUrls()) {
            byteArray.append(url.toEncoded()).append('\n');
        }
        clipboard_.mimeData->setData("x-special/gnome-copied-files", byteArray);
        nautilus.append(byteArray);
        clipboard_.mimeData->setData("COMPOUND_TEXT", nautilus);
        clipboard_.mimeData->setText(item.getText());
        clipboard_.mimeData->setData("text/plain;charset=utf-8", nautilus);
    }
    clipboard_.mimeData->setUrls(item.getUrls());
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
            handleEnterKey();
            break;
        case Qt::Key_Left:
        case Qt::Key_Right:
            handleNavigationKeys(event);
            break;
        case Qt::Key_Home:
        case Qt::Key_End:
            handleHomeEndKeys(event);
            break;
        default:
            handleSearchInput(event);
            break;
    }
}

void MPasteWidget::handleEscapeKey() {
    if (ui_.ui->searchEdit->hasFocus()) {
        ui_.ui->searchEdit->clear();
        setFocusOnSearch(false);
    } else {
        hide();
    }
}

void MPasteWidget::handleEnterKey() {
    currItemsWidget()->selectedByEnter();
    hideAndPaste();
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
            currItemsWidget()->moveToFirst();
        } else {
            currItemsWidget()->moveToLast();
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

bool MPasteWidget::handleAltNumShortcut(QKeyEvent *event) {
#ifdef Q_OS_WIN
    bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (altPressed) {
        int keyOrder = -1;
        if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9) {
            keyOrder = (event->key() == Qt::Key_0) ? 9 : event->key() - Qt::Key_1;
        }

        if (keyOrder >= 0 && keyOrder <= 9) {
            qDebug() << "Alt+" << keyOrder << " detected";
            const ClipboardItem& selectedItem = currItemsWidget()->selectedByShortcut(keyOrder);
            this->setClipboard(selectedItem);
            hideAndPaste();
            currItemsWidget()->cleanShortCutInfo();
            return true;
        }
    }
#else
    if (event->modifiers() & Qt::AltModifier) {
        int keyOrder = misc_.numKeyList.indexOf(event->key());
        if (keyOrder >= 0 && keyOrder <= 9) {
            currItemsWidget()->selectedByShortcut(keyOrder);
            hideAndPaste();
            currItemsWidget()->cleanShortCutInfo();
            return true;
        }
    }
#endif
    return false;
}

// 事件处理相关
bool MPasteWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel) {
#if (QT_VERSION >= QT_VERSION_CHECK(5,12,0))
        QCoreApplication::sendEvent(this->currItemsWidget()->horizontalScrollbar(), event);
        return true;
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
    if (handleAltNumShortcut(event)) {
        event->accept();
        return;
    }
    handleKeyboardEvent(event);
}

void MPasteWidget::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt) {
        currItemsWidget()->cleanShortCutInfo();
    }
    QWidget::keyReleaseEvent(event);
}

void MPasteWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    activateWindow();
    raise();
    setFocus();
}

// 辅助功能实现
void MPasteWidget::loadFromSaveDir() {
    ui_.clipboardWidget->loadFromSaveDir();
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
    return ui_.clipboardWidget;
}

void MPasteWidget::updateItemCount(int itemCount) {
    ui_.ui->countArea->setText(QString::number(itemCount));
}

void MPasteWidget::hideAndPaste() {
    hide();

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
        clipboard_.isPasting = true;
        PlatformRelated::triggerPasteShortcut();
        QTimer::singleShot(200, this, [this]() {
            clipboard_.isPasting = false;
        });
    }
}

void MPasteWidget::setVisibleWithAnnimation(bool visible) {
    if (visible == isVisible()) return;

    if (visible) {
        setWindowOpacity(0);
        show();

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