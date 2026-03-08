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

// 杈呭姪鍑芥暟淇濇寔涓嶅彉
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

void configureOpenGLBackend() {
    const QString backend = qEnvironmentVariable("MPASTE_OPENGL_BACKEND").trimmed().toLower();

    if (backend.isEmpty() || backend == "auto" || backend == "default") {
        return;
    }

    if (backend == "gles") {
        QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
        return;
    }

    if (backend == "software") {
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
        return;
    }

    if (backend == "software-gles" || backend == "gles-software" || backend == "compat") {
        QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
        return;
    }

    qWarning() << "Unknown MPASTE_OPENGL_BACKEND value:" << backend
               << "expected one of: auto, gles, software, software-gles";
}

int main(int argc, char* argv[]) {
    // 璁剧疆 OpenGL 鐩稿叧灞炴€э紝蹇呴』鍦ㄥ垱寤?QApplication 涔嬪墠璁剧疆
    configureOpenGLBackend();

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

        // 鍚姩鍓嶅彴绐楀彛杩借釜
        PlatformRelated::startWindowTracking();

        HotkeyManager hotkeyManager;
        QString shortcutStr = MPasteSettings::getInst()->getShortcutStr();
        if (!hotkeyManager.registerHotkey(QKeySequence(shortcutStr))) {
            qWarning() << "Failed to register global hotkey" << shortcutStr;
        }

        bool isShowingWidget = false;

        auto showWidget = [&widget, &isShowingWidget]() {
            WId focusWinId = PlatformRelated::previousActiveWindow();
            if (!focusWinId) {
                focusWinId = PlatformRelated::currActiveWindow();
            }
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

                // 纭繚绐楀彛鑾峰緱鐒︾偣
                PlatformRelated::activateWindow(widget.winId());
            });
        };

        // 淇敼搴旂敤绋嬪簭鐘舵€佸彉鍖栫殑澶勭悊
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

        // 杩炴帴蹇嵎閿彉鏇?
        QObject::connect(&widget, &MPasteWidget::shortcutChanged,
            [&hotkeyManager](const QString &newShortcut) {
                hotkeyManager.unregisterHotkey();
                hotkeyManager.registerHotkey(QKeySequence(newShortcut));
            });

        // 杩炴帴鐑敭鍜屾秷鎭鐞?
        QObject::connect(&hotkeyManager, &HotkeyManager::hotkeyPressed, showWidget);
        QObject::connect(&singleApp, &SingleApplication::messageReceived,
            [showWidget](const QString &) { showWidget(); });

        return app.exec();
    } else {
        singleApp.sendMessage("show");
        return 0;
    }
}
