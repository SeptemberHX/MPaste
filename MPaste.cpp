//
// Created by ragdoll on 2021/5/22.
//

#include <QApplication>
#include <QDesktopWidget>
#include <iostream>
#include "widget/MPasteWidget.h"
#include "qhotkey/qhotkey.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    app.setApplicationName("MPaste");

    MPasteWidget widget;
    widget.setWindowTitle("MPaste");
    widget.setFixedWidth(QApplication::desktop()->rect().width());

    auto hotkey = new QHotkey(QKeySequence("ctrl+alt+Q"), true, &app);
    QObject::connect(hotkey, &QHotkey::activated, qApp, [&]() {
       widget.setVisible(!widget.isVisible());
       widget.move(0, QApplication::desktop()->height() - widget.height());
    });

    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, qApp, [&](Qt::ApplicationState state) {
        if (state == Qt::ApplicationInactive) {
            widget.hide();
        }
    });

    return app.exec();
}
