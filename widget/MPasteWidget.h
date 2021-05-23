#ifndef MPASTEWIDGET_H
#define MPASTEWIDGET_H

#include <QWidget>
#include "ClipboardMonitor.h"
#include "ClipboardItemWidget.h"
#include "ClipboardItem.h"
#include <QHBoxLayout>
#include <QMimeData>

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

private slots:
    void itemClicked();
    void itemDoubleClicked();
    void clipboardUpdated(ClipboardItem item, int wId);

private:
    void setCurrentItem(ClipboardItemWidget *item);
    void setSelectedItem(ClipboardItemWidget *item);
    void setClipboard(const ClipboardItem &item);

    Ui::MPasteWidget *ui;
    QHBoxLayout *layout;
    ClipboardMonitor *monitor;

    ClipboardItemWidget *currItemWidget;
    QMimeData *mimeData;
};

#endif // MPASTEWIDGET_H
