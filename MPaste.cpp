#include <QApplication>
#include <iostream>
#include <QScreen>
#include "utils/MPasteSettings.h"
#include "widget/MPasteWidget.h"
#include "utils/SingleApplication.h"
#include "utils/PlatformRelated.h"
#include "utils/HotkeyManager.h"

int main(int argc, char* argv[]) {
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
        translator.load("/usr/share/MPaste/translations/MPaste_"+ locale +".qm");
        app.installTranslator(&translator);

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

        // Handle hotkey press
        QObject::connect(&hotkeyManager, &HotkeyManager::hotkeyPressed, [&widget]() {
            MPasteSettings::getInst()->setCurrFocusWinId(PlatformRelated::currActiveWindow());

            auto screen = qApp->screenAt(QCursor::pos());
            widget.setFixedWidth(screen->availableSize().width());
            widget.setVisibleWithAnnimation(!widget.isVisible());
            widget.move(screen->availableGeometry().x(),
                      screen->size().height() - widget.height());
        });

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