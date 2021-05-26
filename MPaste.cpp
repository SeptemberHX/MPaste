//
// Created by ragdoll on 2021/5/22.
//

#include <QApplication>
#include <QDesktopWidget>
#include <iostream>
#include <QScreen>
#include <KWindowSystem>
#include "widget/MPasteWidget.h"
#include "KDSingleApplication/kdsingleapplication.h"
#include "XUtils.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    KDSingleApplication kds;

    if (kds.isPrimaryInstance()) {
        app.setApplicationName("MPaste");
        app.setWindowIcon(QIcon(":/resources/resources/paste.svg"));

        MPasteWidget widget;
        widget.setWindowTitle("MPaste");
        widget.setFixedWidth(QApplication::desktop()->rect().width());

        QObject::connect(qApp, &QGuiApplication::applicationStateChanged, qApp, [&](Qt::ApplicationState state) {
            if (state == Qt::ApplicationInactive) {
                widget.hide();
            }
        });
        XUtils::activeWindowX11(widget.winId());

        QObject::connect(&kds, &KDSingleApplication::messageReceived, qApp, [&] (const QByteArray &message) {
            // whatever received here, just raise the window !
            auto screen = qApp->screenAt(QCursor::pos());
            widget.setFixedWidth(screen->availableSize().width());
            widget.setVisibleWithAnnimation(!widget.isVisible());
            if (widget.isVisible()) {
                // The widget cannot get focus with widget.setFocus(). We have to use x11 directly
                XUtils::activeWindowX11(widget.winId());
            }
            widget.move(screen->availableGeometry().x(), screen->size().height() - widget.height());
        });

        return app.exec();
    } else {
        kds.sendMessage("MPaste");
        return 0;
    }
}
