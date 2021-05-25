#ifndef CLIPBOARDITEMINNERWIDGET_H
#define CLIPBOARDITEMINNERWIDGET_H

#include <QFrame>
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include "MTextBrowser.h"
#include "data/ClipboardItem.h"

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
    void showItem(const ClipboardItem& item);

    void showBorder(bool flag);
    void setShortkeyInfo(int num);
    void clearShortkeyInfo();

private:
    void refreshStyleSheet();
    void showHtml(const QString &html);
    void showImage(const QPixmap &pixmap);
    void showText(const QString &text);
    void showColor(const QColor &color);
    void showUrls(const QList<QUrl> &urls);

    Ui::ClipboardItemInnerWidget *ui;
    QColor bgColor;
    QColor topBgColor;

    MTextBrowser *textBrowser;
    QLabel *imageLabel;
    QHBoxLayout *mLayout;
    int borderWidth;
};

#endif // CLIPBOARDITEMINNERWIDGET_H
