#include <QApplication>
#include <iostream>
#include <QScreen>
#include <qsurfaceformat.h>

#include "utils/MPasteSettings.h"
#include "widget/MPasteWidget.h"
#include "utils/SingleApplication.h"
#include "utils/PlatformRelated.h"
#include "utils/HotkeyManager.h"

// 添加一个辅助函数来获取窗口所在的屏幕
QScreen* getScreenForWindow(WId windowId) {
    if (windowId) {
#ifdef Q_OS_WIN
        HWND hwnd = (HWND)windowId;
        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
            // 使用窗口中心点来确定屏幕
            QPoint windowCenter((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
            return QGuiApplication::screenAt(windowCenter);
        }
#endif
    }
    return QGuiApplication::primaryScreen();
}


int main(int argc, char* argv[]) {
    // 设置 OpenGL 相关属性，必须在创建 QApplication 之前设置
    QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);  // 使用 OpenGL ES
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);  // 如果硬件加速有问题可以尝试软件渲染

    // 设置 OpenGL 格式
    QSurfaceFormat format;
    format.setSwapInterval(1);  // 启用垂直同步
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);  // 双缓冲
    QSurfaceFormat::setDefaultFormat(format);

    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "1");

    QApplication app(argc, argv);

    SingleApplication singleApp("com.mpaste.app");

    if (singleApp.isPrimaryInstance()) {
        app.setApplicationName("MPaste");
        app.setWindowIcon(QIcon::fromTheme("mpaste"));
        QNetworkProxyFactory::setUseSystemConfiguration(true);

        // Load translations
        QString locale = QLocale::system().name();
        QTranslator translator;

        // 获取应用程序可执行文件所在目录
        QString appDir = QCoreApplication::applicationDirPath();

        // 尝试多个可能的翻译文件位置
        QStringList searchPaths = {
            appDir + "/translations/MPaste_" + locale + ".qm",              // 直接在 translations 子目录下
            appDir + "/../translations/MPaste_" + locale + ".qm",          // 上一级目录的 translations 下
            QStringLiteral(":/translations/MPaste_") + locale + ".qm"      // 资源文件中的翻译
        };

        bool loaded = false;
        for (const QString &path : searchPaths) {
            if (translator.load(path)) {
                loaded = true;
                break;
            }
        }

        if (loaded) {
            app.installTranslator(&translator);
        } else {
            qWarning() << "Failed to load translation for" << locale;
        }

        // Create and setup main widget
        MPasteWidget widget;
        widget.setWindowTitle("MPaste");
        widget.setFixedWidth(QApplication::primaryScreen()->geometry().width());

        // Create hotkey manager
        HotkeyManager hotkeyManager;
        // Register Alt+V hotkey
        if (!hotkeyManager.registerHotkey(QKeySequence("Alt+V"))) {
            qWarning() << "Failed to register global hotkey Alt+V";
        }

        // 显示窗口的 lambda 函数
        auto showWidget = [&widget]() {
            WId focusWinId = PlatformRelated::currActiveWindow();
            MPasteSettings::getInst()->setCurrFocusWinId(focusWinId);

            // 获取当前活动窗口所在的屏幕
            QScreen* currentScreen = getScreenForWindow(focusWinId);
            if (!currentScreen) {
                currentScreen = QGuiApplication::screenAt(QCursor::pos());
            }
            if (!currentScreen) {
                currentScreen = QGuiApplication::primaryScreen();
            }

            // 设置窗口宽度和位置
            widget.setFixedWidth(currentScreen->availableSize().width());
            widget.setVisibleWithAnnimation(!widget.isVisible());
            widget.move(currentScreen->geometry().x(),
                       currentScreen->geometry().y() +
                       currentScreen->geometry().height() -
                       widget.height());
        };

        // 在主函数中使用这个 lambda
        QObject::connect(&hotkeyManager, &HotkeyManager::hotkeyPressed, showWidget);
        QObject::connect(&singleApp, &SingleApplication::messageReceived,
            [showWidget](const QString &) { showWidget(); });

        // Handle application state changes
        QObject::connect(qApp, &QGuiApplication::applicationStateChanged, 
            [&widget](Qt::ApplicationState state) {
                if (state == Qt::ApplicationInactive) {
                    widget.setVisibleWithAnnimation(false);
                }
            });

        PlatformRelated::activateWindow(widget.winId());

        // Handle messages from other instances
        QObject::connect(&singleApp, &SingleApplication::messageReceived,
            [&widget](const QString &) {
                MPasteSettings::getInst()->setCurrFocusWinId(PlatformRelated::currActiveWindow());
                
                auto screen = qApp->screenAt(QCursor::pos());
                widget.setFixedWidth(screen->availableSize().width());
                widget.setVisibleWithAnnimation(!widget.isVisible());
                widget.move(screen->availableGeometry().x(), 
                          screen->size().height() - widget.height());
            });

        return app.exec();
    } else {
        // If this is not the primary instance, send a message and exit
        singleApp.sendMessage("show");
        return 0;
    }
}