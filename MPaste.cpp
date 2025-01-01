#include <QApplication>
#include <iostream>
#include <QScreen>
#include <qsurfaceformat.h>
#include <QTimer>
#include <QShowEvent>

#include "utils/MPasteSettings.h"
#include "widget/MPasteWidget.h"
#include "utils/SingleApplication.h"
#include "utils/PlatformRelated.h"
#include "utils/HotKeyManager.h"

// 辅助函数保持不变
QScreen* getScreenForWindow(WId windowId) {
    if (windowId) {
#ifdef Q_OS_WIN
        HWND hwnd = (HWND)windowId;
        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
            QPoint windowCenter((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
            return QGuiApplication::screenAt(windowCenter);
        }
#endif
    }
    return QGuiApplication::primaryScreen();
}

int main(int argc, char* argv[]) {
    // 设置 OpenGL 相关属性，必须在创建 QApplication 之前设置
    QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);

    QSurfaceFormat format;
    format.setSwapInterval(1);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
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
        QString appDir = QCoreApplication::applicationDirPath();
        QStringList searchPaths = {
            appDir + "/translations/MPaste_" + locale + ".qm",
            appDir + "/../translations/MPaste_" + locale + ".qm",
            QStringLiteral(":/translations/MPaste_") + locale + ".qm"
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

        MPasteWidget widget;
        widget.setWindowTitle("MPaste");
        widget.setFixedWidth(QApplication::primaryScreen()->geometry().width());

        HotkeyManager hotkeyManager;
        if (!hotkeyManager.registerHotkey(QKeySequence("Alt+V"))) {
            qWarning() << "Failed to register global hotkey Alt+V";
        }

        bool isShowingWidget = false;

        auto showWidget = [&widget, &isShowingWidget]() {
            WId focusWinId = PlatformRelated::currActiveWindow();
            MPasteSettings::getInst()->setCurrFocusWinId(focusWinId);

            QScreen* currentScreen = getScreenForWindow(focusWinId);
            if (!currentScreen) {
                currentScreen = QGuiApplication::screenAt(QCursor::pos());
            }
            if (!currentScreen) {
                currentScreen = QGuiApplication::primaryScreen();
            }

            widget.setFixedWidth(currentScreen->availableSize().width());

            isShowingWidget = true;

            QTimer::singleShot(50, [&widget, currentScreen]() {
                widget.setVisibleWithAnnimation(true);
                widget.raise();
                widget.activateWindow();
                widget.move(currentScreen->geometry().x(),
                          currentScreen->geometry().y() +
                          currentScreen->geometry().height() -
                          widget.height());

                // 确保窗口获得焦点
                PlatformRelated::activateWindow(widget.winId());
            });
        };

        // 修改应用程序状态变化的处理
        QObject::connect(qApp, &QGuiApplication::applicationStateChanged,
            [&widget, &isShowingWidget](Qt::ApplicationState state) {
                if (state == Qt::ApplicationInactive) {
                    QTimer::singleShot(100, [&widget, &isShowingWidget]() {
                        if (isShowingWidget) {
                            widget.setVisibleWithAnnimation(false);
                            isShowingWidget = false;
                        }
                    });
                }
            });

        // 连接热键和消息处理
        QObject::connect(&hotkeyManager, &HotkeyManager::hotkeyPressed, showWidget);
        QObject::connect(&singleApp, &SingleApplication::messageReceived,
            [showWidget](const QString &) { showWidget(); });

        return app.exec();
    } else {
        singleApp.sendMessage("show");
        return 0;
    }
}