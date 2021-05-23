//
// Created by ragdoll on 2021/5/22.
//

#ifndef MPASTE_CLIPBOARDITEMWIDGET_H
#define MPASTE_CLIPBOARDITEMWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include "ClipboardItemInnerWidget.h"
#include "ClipboardItem.h"
#include "ClipboardMonitor.h"

class ClipboardItemWidget : public QWidget{

    Q_OBJECT

public:
    explicit ClipboardItemWidget(QWidget *parent= nullptr);

public slots:
    void showItem(ClipboardItem item);

private:
    QHBoxLayout *layout;

    ClipboardItemInnerWidget *innerShadowedWidget;
    ClipboardItem item;

    ClipboardMonitor *monitor;
};


#endif //MPASTE_CLIPBOARDITEMWIDGET_H