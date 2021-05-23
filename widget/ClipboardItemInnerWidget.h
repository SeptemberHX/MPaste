#ifndef CLIPBOARDITEMINNERWIDGET_H
#define CLIPBOARDITEMINNERWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include "MTextBrowser.h"
#include "ClipboardItem.h"

namespace Ui {
class ClipboardItemInnerWidget;
}

class ClipboardItemInnerWidget : public QWidget
{
    Q_OBJECT

public:
    static QString genStyleSheetStr(QColor bgColor, QColor topColor);

    explicit ClipboardItemInnerWidget(QWidget *parent = nullptr);
    ~ClipboardItemInnerWidget();

    void setIcon(const QPixmap &icon);
    void showItem(ClipboardItem item);

private:
    Ui::ClipboardItemInnerWidget *ui;
    QColor bgColor;

    MTextBrowser *textBrowser;
    QLabel *imageLabel;
    QHBoxLayout *mLayout;
};

#endif // CLIPBOARDITEMINNERWIDGET_H
