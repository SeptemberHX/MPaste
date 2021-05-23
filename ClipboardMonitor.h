//
// Created by ragdoll on 2021/5/22.
//

#ifndef MPASTE_CLIPBOARDMONITOR_H
#define MPASTE_CLIPBOARDMONITOR_H

#include <QObject>
#include "ClipboardItem.h"

class ClipboardMonitor : public QObject {
    Q_OBJECT

public:
    ClipboardMonitor();

signals:
    void clipboardUpdated(ClipboardItem item, int wId);

public slots:
    void clipboardChanged();
};


#endif //MPASTE_CLIPBOARDMONITOR_H
