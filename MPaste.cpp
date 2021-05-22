//
// Created by ragdoll on 2021/5/22.
//

#include <QApplication>
#include "ClipboardMonitor.h"
#include "widget/ClipboardItemWidget.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    ClipboardItemWidget widget;
    widget.show();

    return app.exec();
}
