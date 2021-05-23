//
// Created by ragdoll on 2021/5/23.
//

#include "MTextBrowser.h"
#include <QWheelEvent>

void MTextBrowser::wheelEvent(QWheelEvent *e) {
    e->ignore();
}

MTextBrowser::MTextBrowser(QWidget *parent) : QTextBrowser(parent) {

}
