// input: 依赖 Qt 应用入口、翻译加载、全局热键与主窗口初始化流程。
// output: 提供 MPaste 应用启动、单实例控制和主窗口装配逻辑。
// pos: 项目入口层中的 MPaste 应用入口实现文件。
// update: 修改本文件时，同步更新文件头注释与根目录 README.md。
// note: Prefer QScreen::availableGeometry for sizing/positioning (Wayland scaling can differ from availableSize()).
// note: Set `MPASTE_DEBUG_GEOMETRY=1` to print detailed screen/placement diagnostics.
#include <QApplication>
#include <iostream>
#include <QScreen>
#include <qsurfaceformat.h>
#include <QTimer>
#include <QShowEvent>
#include <QWindow>

#include "utils/MPasteSettings.h"
#include "widget/MPasteWidget.h"
#include "utils/SingleApplication.h"
#include "utils/PlatformRelated.h"
#include "utils/HotKeyManager.h"
#include "utils/ThemeManager.h"

namespace {
bool isWaylandPlatform() {
    const QString platform = QGuiApplication::platformName().toLower();
    return platform.contains(QStringLiteral("wayland"));
}

bool geometryDebugEnabled() {
    static const bool enabled = !qEnvironmentVariableIsEmpty("MPASTE_DEBUG_GEOMETRY");
    return enabled;
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

void logScreenGeometry(const char *tag, QScreen *screen) {
    if (!geometryDebugEnabled() || !screen) {
        return;
    }

    const QRect g = screen->geometry();
    const QRect avail = screen->availableGeometry();
    const QRect norm = availableGeometryForWidget(screen);
    const QSize size = screen->size();
    qInfo().noquote() << QStringLiteral("[geometry] %1 platform=%2 screen=\"%3\" dpr=%4 geom=%5,%6 %7x%8 avail=%9,%10 %11x%12 normAvail=%13,%14 %15x%16")
        .arg(QString::fromLatin1(tag))
        .arg(QGuiApplication::platformName())
        .arg(screen->name())
        .arg(screen->devicePixelRatio(), 0, 'f', 2)
        .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height())
        .arg(avail.x()).arg(avail.y()).arg(avail.width()).arg(avail.height())
        .arg(norm.x()).arg(norm.y()).arg(norm.width()).arg(norm.height());

    qInfo().noquote() << QStringLiteral("[geometry] %1 size=%2x%3 logicalDpi=%4 physicalDpi=%5 refresh=%6")
        .arg(QString::fromLatin1(tag))
        .arg(size.width()).arg(size.height())
        .arg(screen->logicalDotsPerInch(), 0, 'f', 2)
        .arg(screen->physicalDotsPerInch(), 0, 'f', 2)
        .arg(screen->refreshRate(), 0, 'f', 2);
}
}

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
#ifdef Q_OS_WIN
    return QGuiApplication::primaryScreen();
#else
    Q_UNUSED(windowId);
    return QGuiApplication::screenAt(QCursor::pos());
#endif
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

        if (geometryDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("[geometry] platform=%1 screens=%2 primary=\"%3\" cursor=%4,%5")
                .arg(QGuiApplication::platformName())
                .arg(QGuiApplication::screens().size())
                .arg(QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->name() : QStringLiteral("-"))
                .arg(QCursor::pos().x())
                .arg(QCursor::pos().y());
            qInfo().noquote() << QStringLiteral("[geometry] env QT_QPA_PLATFORM=%1 QT_SCALE_FACTOR=%2 QT_SCREEN_SCALE_FACTORS=%3 QT_AUTO_SCREEN_SCALE_FACTOR=%4")
                .arg(QString::fromLocal8Bit(qgetenv("QT_QPA_PLATFORM")))
                .arg(QString::fromLocal8Bit(qgetenv("QT_SCALE_FACTOR")))
                .arg(QString::fromLocal8Bit(qgetenv("QT_SCREEN_SCALE_FACTORS")))
                .arg(QString::fromLocal8Bit(qgetenv("QT_AUTO_SCREEN_SCALE_FACTOR")));
            for (QScreen *screen : QGuiApplication::screens()) {
                logScreenGeometry("screen", screen);
            }
        }

        MPasteWidget widget;
        widget.setWindowTitle("MPaste");
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            widget.setFixedWidth(availableGeometryForWidget(screen).width());
        }

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

            logScreenGeometry("showWidget.screen", currentScreen);
            const QRect available = availableGeometryForWidget(currentScreen);
            const bool wayland = isWaylandPlatform();
            if (wayland) {
                widget.prepareWaylandDock(available);
            } else {
                widget.setFixedWidth(available.width());
            }

            isShowingWidget = true;

            const int showDelayMs = immediateForAltHotkey ? 0 : 50;
            QTimer::singleShot(showDelayMs, [&widget, available, wayland]() {
                widget.setVisibleWithAnnimation(true);
                widget.raise();
                widget.activateWindow();
                if (!wayland && available.isValid()) {
                    widget.move(available.x(),
                              available.y() +
                              available.height() -
                              widget.height());
                }

                if (geometryDebugEnabled()) {
                    qInfo().noquote() << QStringLiteral("[geometry] showWidget.move avail=%1,%2 %3x%4 widgetH=%5 pos=%6,%7 geom=%8,%9 %10x%11")
                        .arg(available.x()).arg(available.y()).arg(available.width()).arg(available.height())
                        .arg(widget.height())
                        .arg(widget.pos().x()).arg(widget.pos().y())
                        .arg(widget.geometry().x()).arg(widget.geometry().y())
                        .arg(widget.geometry().width()).arg(widget.geometry().height());
                    qInfo().noquote() << QStringLiteral("[geometry] widget dpr=%1")
                        .arg(widget.devicePixelRatioF(), 0, 'f', 2);
                    if (QWindow *handle = widget.windowHandle()) {
                        const QRect winGeom = handle->geometry();
                        qInfo().noquote() << QStringLiteral("[geometry] windowHandle geom=%1,%2 %3x%4 screen=\"%5\"")
                            .arg(winGeom.x()).arg(winGeom.y()).arg(winGeom.width()).arg(winGeom.height())
                            .arg(handle->screen() ? handle->screen()->name() : QStringLiteral("-"));
                    }
                }

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
