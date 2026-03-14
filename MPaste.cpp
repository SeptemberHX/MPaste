// input: 依赖 Qt 应用入口、翻译加载、全局热键与主窗口初始化流程。
// output: 提供 MPaste 应用启动、单实例控制和主窗口装配逻辑。
// pos: 项目入口层中的 MPaste 应用入口实现文件。
// update: 修改本文件时，同步更新文件头注释与根目录 README.md。
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
#include "utils/ThemeManager.h"

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

#ifdef Q_OS_WIN
template <typename Func>
void runAfterAltReleased(QObject *context, Func &&func, int intervalMs = 10, int maxPollCount = 50) {
    auto *timer = new QTimer(context);
    auto *pollCount = new int(0);
    timer->setInterval(intervalMs);
    QObject::connect(timer, &QTimer::timeout, context, [timer, pollCount, fn = std::forward<Func>(func), maxPollCount]() mutable {
        const bool altReleased = (GetAsyncKeyState(VK_MENU) & 0x8000) == 0;
        const bool timedOut = *pollCount >= maxPollCount;
        if (altReleased || timedOut) {
            timer->stop();
            timer->deleteLater();
            delete pollCount;
            fn();
            return;
        }

        ++(*pollCount);
    });
    timer->start();
}
#endif

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

        ThemeManager::instance()->initialize();

        MPasteWidget widget;
        widget.setWindowTitle("MPaste");
        widget.setFixedWidth(QApplication::primaryScreen()->geometry().width());

        PlatformRelated::startWindowTracking();

        HotkeyManager hotkeyManager;
        QString shortcutStr = MPasteSettings::getInst()->getShortcutStr();
        if (!hotkeyManager.registerHotkey(QKeySequence(shortcutStr))) {
            qWarning() << "Failed to register global hotkey" << shortcutStr;
        }

        bool isShowingWidget = false;

        auto shortcutUsesAlt = []() {
            const QKeySequence shortcut(MPasteSettings::getInst()->getShortcutStr());
            if (shortcut.isEmpty()) {
                return false;
            }
            const auto modifiers = Qt::KeyboardModifiers(shortcut[0] & Qt::KeyboardModifierMask);
            return modifiers.testFlag(Qt::AltModifier);
        };

        auto showWidget = [&widget, &isShowingWidget](bool immediateForAltHotkey = false) {
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

            const int showDelayMs = immediateForAltHotkey ? 0 : 50;
            QTimer::singleShot(showDelayMs, [&widget, currentScreen]() {
                widget.setVisibleWithAnnimation(true);
                widget.raise();
                widget.activateWindow();
                widget.move(currentScreen->geometry().x(),
                          currentScreen->geometry().y() +
                          currentScreen->geometry().height() -
                          widget.height());

                PlatformRelated::activateWindow(widget.winId());
            });
        };

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


        auto toggleWidget = [&widget, &isShowingWidget, showWidget, shortcutUsesAlt]() {
            const bool altHotkey = shortcutUsesAlt();
            if (widget.isVisible() || isShowingWidget) {
                auto hideWidget = [&widget, &isShowingWidget]() {
                    widget.setVisibleWithAnnimation(false);
                    isShowingWidget = false;
                };
#ifdef Q_OS_WIN
                if (altHotkey) {
                    runAfterAltReleased(&widget, hideWidget);
                } else {
                    hideWidget();
                }
#else
                hideWidget();
#endif
                return;
            }
            showWidget(altHotkey);
        };
        QObject::connect(&widget, &MPasteWidget::shortcutChanged,
            [&hotkeyManager](const QString &newShortcut) {
                hotkeyManager.unregisterHotkey();
                hotkeyManager.registerHotkey(QKeySequence(newShortcut));
            });

        QObject::connect(&hotkeyManager, &HotkeyManager::hotkeyPressed, toggleWidget);
        QObject::connect(&singleApp, &SingleApplication::messageReceived,
            [showWidget](const QString &) { showWidget(); });

        return app.exec();
    } else {
        singleApp.sendMessage("show");
        return 0;
    }
}
