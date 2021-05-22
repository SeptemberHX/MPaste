//
// Created by ragdoll on 2021/5/22.
//

#include <QApplication>
#include "ClipboardMonitor.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    ClipboardMonitor monitor;

    return app.exec();
}
