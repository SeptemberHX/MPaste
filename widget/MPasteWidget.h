#ifndef MPASTEWIDGET_H
#define MPASTEWIDGET_H

#include <QWidget>
#include "ClipboardMonitor.h"
#include <QHBoxLayout>

namespace Ui {
class MPasteWidget;
}

class MPasteWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MPasteWidget(QWidget *parent = nullptr);
    ~MPasteWidget();

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Ui::MPasteWidget *ui;
    QHBoxLayout *layout;
    ClipboardMonitor *monitor;
};

#endif // MPASTEWIDGET_H
