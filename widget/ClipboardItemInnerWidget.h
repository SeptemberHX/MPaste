#ifndef CLIPBOARDITEMINNERWIDGET_H
#define CLIPBOARDITEMINNERWIDGET_H

#include <QFrame>
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include "MTextBrowser.h"
#include "ClipboardItem.h"

namespace Ui {
class ClipboardItemInnerWidget;
}

class ClipboardItemInnerWidget : public QFrame
{
    Q_OBJECT

public:
    static QString genStyleSheetStr(QColor bgColor, QColor topColor, int bw);

    explicit ClipboardItemInnerWidget(QWidget *parent = nullptr);
    ~ClipboardItemInnerWidget();

    void setIcon(const QPixmap &icon);
    void showItem(ClipboardItem item);

    void showBorder(bool flag);
    void setShortkeyInfo(int num);
    void clearShortkeyInfo();

private:
    void refreshStyleSheet();

    Ui::ClipboardItemInnerWidget *ui;
    QColor bgColor;
    QColor topBgColor;

    MTextBrowser *textBrowser;
    QLabel *imageLabel;
    QHBoxLayout *mLayout;
    int borderWidth;
};

#endif // CLIPBOARDITEMINNERWIDGET_H
