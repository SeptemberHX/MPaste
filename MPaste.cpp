//
// Created by ragdoll on 2021/5/22.
//

#include <QApplication>
#include <QDesktopWidget>
#include <iostream>
#include <QNetworkProxy>
#include <QScreen>
#include <utils/MPasteSettings.h>
#include "widget/MPasteWidget.h"
#include "KDSingleApplication/kdsingleapplication.h"
#include "utils/PlatformRelated.h"

int main(int argc, char* argv[]) {
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "1");
    QApplication app(argc, argv);
    KDSingleApplication kds;

    if (kds.isPrimaryInstance()) {
        app.setApplicationName("MPaste");
        app.setWindowIcon(QIcon::fromTheme("mpaste"));
        QNetworkProxyFactory::setUseSystemConfiguration(true);

        QString locale = QLocale::system().name();
        QTranslator translator;
        translator.load("/usr/share/MPaste/translations/MPaste_"+ locale +".qm");
        app.installTranslator(&translator);

        MPasteWidget widget;
        widget.setWindowTitle("MPaste");
        widget.setFixedWidth(QApplication::desktop()->rect().width());

        QObject::connect(qApp, &QGuiApplication::applicationStateChanged, qApp, [&](Qt::ApplicationState state) {
            if (state == Qt::ApplicationInactive) {
                widget.setVisibleWithAnnimation(false);
            }
        });
        PlatformRelated::activateWindow(widget.winId());

        QObject::connect(&kds, &KDSingleApplication::messageReceived, qApp, [&] (const QByteArray &message) {
            MPasteSettings::getInst()->setCurrFocusWinId(PlatformRelated::currActiveWindow());
            // whatever received here, just raise the window !
            auto screen = qApp->screenAt(QCursor::pos());
            widget.setFixedWidth(screen->availableSize().width());
            widget.setVisibleWithAnnimation(!widget.isVisible());
            widget.move(screen->availableGeometry().x(), screen->size().height() - widget.height());
        });

        return app.exec();
    } else {
        kds.sendMessage("MPaste");
        return 0;
    }
}
