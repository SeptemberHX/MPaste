// input: 依赖 Qt 应用入口、翻译加载、全局热键与主窗口初始化流程。
// output: 提供启动、单实例、--show 入口、鼠标所在屏幕呼出，以及 GNOME Wayland 桥接缺失提示。
// pos: 项目入口层中的 MPaste 应用入口实现文件。
// update: 修改本文件时，同步更新文件头注释与根目录 README.md。
#include <QApplication>
#include <iostream>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QScreen>
#include <qsurfaceformat.h>
#include <QTimer>
#include <QShowEvent>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QMutex>
#include <QTextStream>
#include <QMessageBox>
#ifdef Q_OS_LINUX
#include "utils/GnomeBridge.h"
#endif

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

QString chooseInstalledUiFontFamily(const QStringList &preferredFamilies, const QString &fallbackFamily) {
    const QFontDatabase database;
    const QStringList availableFamilies = database.families();
    for (const QString &family : preferredFamilies) {
        if (availableFamilies.contains(family, Qt::CaseInsensitive)) {
            return family;
        }
    }
    return fallbackFamily;
}

void configureApplicationFont(QApplication &app) {
#ifdef Q_OS_WIN
    QFont font = app.font();
    const QString chosenFamily = chooseInstalledUiFontFamily(
        {
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Microsoft YaHei"),
            QStringLiteral("Segoe UI"),
            QStringLiteral("Noto Sans"),
            QStringLiteral("Arial")
        },
        font.family());
    font.setFamily(chosenFamily);
    font.setStyleHint(QFont::SansSerif, QFont::PreferDefault);
    app.setFont(font);
#else
    // Elsewhere the platform theme (qt6ct, GNOME) already supplies the
    // desktop UI font; forcing YaHei there loses its medium weights.
    Q_UNUSED(app);
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

namespace {
QFile *g_logFile = nullptr;
QMutex g_logMutex;

void mpasteMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    Q_UNUSED(context);
    const char *level = "INFO";
    switch (type) {
        case QtDebugMsg:    level = "DEBUG"; break;
        case QtInfoMsg:     level = "INFO";  break;
        case QtWarningMsg:  level = "WARN";  break;
        case QtCriticalMsg: level = "ERROR"; break;
        case QtFatalMsg:    level = "FATAL"; break;
    }
    const QString line = QStringLiteral("%1 [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
        .arg(QString::fromLatin1(level))
        .arg(msg);

    QMutexLocker locker(&g_logMutex);
    if (g_logFile && g_logFile->isOpen()) {
        g_logFile->write(line.toUtf8());
        g_logFile->flush();
    }
    // Also echo to stderr (visible when launched from a console).
    fputs(line.toUtf8().constData(), stderr);

    if (type == QtFatalMsg) {
        abort();
    }
}

void installFileLogger() {
    const QString dirPath = qEnvironmentVariable("MPASTE_LOG_DIR",
        QDir::homePath() + QDir::separator() + QStringLiteral(".MPaste"));
    QDir().mkpath(dirPath);
    const QString logPath = dirPath + QDir::separator() + QStringLiteral("MPaste.log");

    // Rotate if larger than 5 MB to keep the file bounded.
    QFileInfo info(logPath);
    if (info.exists() && info.size() > 5 * 1024 * 1024) {
        const QString rotated = logPath + QStringLiteral(".1");
        QFile::remove(rotated);
        QFile::rename(logPath, rotated);
    }

    g_logFile = new QFile(logPath);
    if (!g_logFile->open(QIODevice::Append | QIODevice::Text)) {
        delete g_logFile;
        g_logFile = nullptr;
        return;
    }
    qInstallMessageHandler(mpasteMessageHandler);
    qInfo().noquote() << QStringLiteral("=== MPaste log opened at %1 ===")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
}
}  // namespace

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
    installFileLogger();
    configureOpenGLBackend();

    QSurfaceFormat format;
    format.setSwapInterval(1);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);

    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "1");

    QApplication app(argc, argv);
    configureApplicationFont(app);
    SingleApplication singleApp("com.mpaste.app");

    if (singleApp.isPrimaryInstance()) {
        app.setApplicationName("MPaste");
        app.setWindowIcon(QIcon(":/resources/resources/mpaste.svg"));
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
            QElapsedTimer wakeTimer;
            wakeTimer.start();

            WId focusWinId = PlatformRelated::previousActiveWindow();
            if (!focusWinId) {
                focusWinId = PlatformRelated::currActiveWindow();
            }
            MPasteSettings::getInst()->setCurrFocusWinId(focusWinId);

            QScreen* currentScreen = QGuiApplication::screenAt(QCursor::pos());
            if (!currentScreen) {
                currentScreen = getScreenForWindow(focusWinId);
            }
            if (!currentScreen) {
                currentScreen = QGuiApplication::primaryScreen();
            }

            // GNOME places the panel on the pointer's monitor before
            // painting. A primary-screen fixed width prevents Mutter from
            // resizing it when the next invocation targets another monitor.
#ifdef Q_OS_LINUX
            if (GnomeBridge::isWayland()) {
                widget.setMinimumWidth(0);
                widget.setMaximumWidth(QWIDGETSIZE_MAX);
                widget.resize(currentScreen->availableSize().width(), widget.height());
            } else
#endif
            {
                widget.setFixedWidth(currentScreen->availableSize().width());
            }

            isShowingWidget = true;

            qInfo().noquote() << QStringLiteral("[wake] showWidget prep done: %1 ms").arg(wakeTimer.elapsed());

            const int showDelayMs = immediateForAltHotkey ? 0 : 50;
            QTimer::singleShot(showDelayMs, [&widget, currentScreen, wakeTimer]() {
                qInfo().noquote() << QStringLiteral("[wake] timer fired: %1 ms").arg(wakeTimer.elapsed());
                widget.setVisibleWithAnnimation(true);
                qInfo().noquote() << QStringLiteral("[wake] setVisibleWithAnnimation done: %1 ms").arg(wakeTimer.elapsed());
                widget.raise();
                widget.activateWindow();
                widget.move(currentScreen->geometry().x(),
                          currentScreen->geometry().y() +
                          currentScreen->geometry().height() -
                          widget.height());

                PlatformRelated::activateWindow(widget.winId());
                qInfo().noquote() << QStringLiteral("[wake] showWidget complete: %1 ms").arg(wakeTimer.elapsed());
            });
        };

        QObject::connect(qApp, &QGuiApplication::applicationStateChanged,
            [&widget, &isShowingWidget](Qt::ApplicationState state) {
                if (state == Qt::ApplicationInactive) {
                    QTimer::singleShot(100, [&widget, &isShowingWidget]() {
                        if (QGuiApplication::applicationState() != Qt::ApplicationActive)
                            widget.hideQuickStack();
                        if (isShowingWidget) {
                            widget.setVisibleWithAnnimation(false);
                            isShowingWidget = false;
                        }
                    });
                }
            });


        auto toggleWidget = [&widget, &isShowingWidget, showWidget, shortcutUsesAlt]() {
            qInfo().noquote() << QStringLiteral("[wake] toggleWidget entered");
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
            if (MPasteSettings::getInst()->getActivationLayout() == MPasteSettings::LayoutStack) {
                if (widget.isQuickStackVisible()) widget.hideQuickStack();
                else widget.showQuickStack();
                return;
            }
            showWidget(altHotkey);
        };
        // Tab in the stack opens the full panel for the same paste target.
        QObject::connect(&widget, &MPasteWidget::expandToPanelRequested,
                         [showWidget]() { showWidget(false); });
        QObject::connect(&widget, &MPasteWidget::shortcutChanged,
            [&hotkeyManager, &widget, &shortcutStr](const QString &newShortcut) {
                hotkeyManager.unregisterHotkey();
                if (!hotkeyManager.registerHotkey(QKeySequence(newShortcut))) {
                    hotkeyManager.registerHotkey(QKeySequence(shortcutStr));
                    MPasteSettings::getInst()->setShortcutStr(shortcutStr);
                    QMessageBox::warning(&widget, QStringLiteral("MPaste"),
                        QStringLiteral("快捷键注册失败。请检查 GNOME 设置 → 键盘 → 自定义快捷键，"
                                       "确认没有其他程序占用该组合键。已保留原快捷键。"));
                } else {
                    shortcutStr = newShortcut;
                }
            });

        QObject::connect(&hotkeyManager, &HotkeyManager::hotkeyPressed, toggleWidget);
        QObject::connect(&singleApp, &SingleApplication::messageReceived,
            [showWidget, toggleWidget](const QString &message) {
                if (message == QStringLiteral("toggle")) toggleWidget();
                else showWidget();
            });

        if (app.arguments().contains(QStringLiteral("--show")) ||
            app.arguments().contains(QStringLiteral("--toggle"))) {
            QTimer::singleShot(0, &widget, [showWidget]() { showWidget(); });
        }
#ifdef Q_OS_LINUX
        if (GnomeBridge::isWayland()) {
            const auto reply = GnomeBridge::instance()->call("CurrentWindow");
            if (reply.type() == QDBusMessage::ErrorMessage) {
                QTimer::singleShot(0, &widget, [&widget]() {
                    QMessageBox::warning(&widget, QStringLiteral("MPaste — GNOME Wayland"),
                        QStringLiteral("Enable the MPaste Wayland Integration GNOME extension, "
                                       "then log out and log back in.\n\n"
                                       "Setup: python3 tools/install_gnome_wayland.py\n\n"
                                       "Background clipboard capture, the global shortcut and automatic "
                                       "paste require this extension on GNOME Wayland."));
                });
            }
        }
#endif
        return app.exec();
    } else {
        singleApp.sendMessage(app.arguments().contains(QStringLiteral("--toggle")) ? "toggle" : "show");
        return 0;
    }
}
