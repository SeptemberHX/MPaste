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

    const ClipboardItem &getItem() const;

signals:
    void clicked();
    void doubleClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

public slots:
    void showItem(ClipboardItem item);
    void setSelected(bool selected);

private:
    QHBoxLayout *layout;

    ClipboardItemInnerWidget *innerShadowedWidget;
    ClipboardItem item;

    ClipboardMonitor *monitor;
};


#endif //MPASTE_CLIPBOARDITEMWIDGET_H
