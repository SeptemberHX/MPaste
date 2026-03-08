// input: 依赖 Qt Widgets、data 层对象与同层组件声明。
// output: 对外提供 ClipboardItemInnerWidget 的声明接口。
// pos: widget 层中的 ClipboardItemInnerWidget 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
#ifndef CLIPBOARDITEMINNERWIDGET_H
#define CLIPBOARDITEMINNERWIDGET_H

#include <QFrame>
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include "MTextBrowser.h"
#include "data/ClipboardItem.h"
#include "FileThumbWidget.h"
#include "WebLinkThumbWidget.h"

namespace Ui {
class ClipboardItemInnerWidget;
}

class ClipboardItemInnerWidget : public QFrame
{
    Q_OBJECT

public:
    static QString genStyleSheetStr(QColor bgColor, QColor topColor, QColor borderColor, int bw);

    explicit ClipboardItemInnerWidget(QColor borderColor, QWidget *parent = nullptr);
    ~ClipboardItemInnerWidget();

    void setIcon(const QPixmap &icon);
    void showItem(const ClipboardItem& item);

    void showBorder(bool flag);
    void setShortkeyInfo(int num);
    void clearShortkeyInfo();

signals:
    void itemNeedToSave(const ClipboardItem &item);

private:
    void refreshStyleSheet();
    void resetPanelStyleOverrides();
    void showHtml(const QString &html);
    void showImage(const QPixmap &pixmap);
    void showText(const QString &text, const ClipboardItem &item);
    void showColor(const QColor &color, const QString &rawStr);
    void showUrls(const QList<QUrl> &urls, const ClipboardItem &item);
    void showWebLink(const QUrl &url, const ClipboardItem &item);
    void showFile(const QUrl &url);
    void showFiles(const QList<QUrl> &fileUrls);

    void initTextBrowser();
    void initImageLabel();
    void initFileThumbWidget();
    void initWebLinkThumbWidget();

    bool checkWebLink(const QString &str);

    Ui::ClipboardItemInnerWidget *ui;
    QColor bgColor;
    QColor topBgColor;

    MTextBrowser *textBrowser;
    QLabel *imageLabel;
    QHBoxLayout *mLayout;
    int borderWidth;
    FileThumbWidget *fileThumbWidget;
    WebLinkThumbWidget *webLinkThumbWidget;

    QColor borderColor;
};

#endif // CLIPBOARDITEMINNERWIDGET_H
