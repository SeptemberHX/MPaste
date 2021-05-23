//
// Created by ragdoll on 2021/5/22.
//

#include <QApplication>
#include "widget/MPasteWidget.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    MPasteWidget widget;
    widget.show();

    return app.exec();
}
