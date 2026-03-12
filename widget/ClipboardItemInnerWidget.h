// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 ClipboardItemInnerWidget 的声明接口。
// pos: widget 层中的 ClipboardItemInnerWidget 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#ifndef CLIPBOARDITEMINNERWIDGET_H
#define CLIPBOARDITEMINNERWIDGET_H

#include <QFrame>
#include <QWidget>
#include <QByteArray>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QUrl>
#include "MTextBrowser.h"
#include "data/ClipboardItem.h"
#include "FileThumbWidget.h"
#include "WebLinkThumbWidget.h"

class QNetworkAccessManager;
class QNetworkReply;
class QPaintEvent;

namespace Ui {
class ClipboardItemInnerWidget;
}

class ClipboardItemInnerWidget : public QFrame
{
    Q_OBJECT

public:
    static QString genStyleSheetStr(QColor bgColor, QColor topColor, QColor borderColor, int bw);
    static QPixmap buildCardThumbnail(const QPixmap &pixmap);

    explicit ClipboardItemInnerWidget(QColor borderColor, QWidget *parent = nullptr);
    ~ClipboardItemInnerWidget();

    void setIcon(const QPixmap &icon);
    void showItem(const ClipboardItem& item);

    void showBorder(bool flag);
    void setFavoriteHighlight(bool flag);
    void setShortkeyInfo(int num);
    void clearShortkeyInfo();

signals:
    void itemNeedToSave(const ClipboardItem &item);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void hideContentWidgets();
    void prepareTextBrowserDocument();
    void refreshStyleSheet();
    void resetPanelStyleOverrides();
    void setInfoWidgetVisible(bool visible);
    void showHtmlSnapshot(const QPixmap &pixmap, int charCount);
    void showHtml(const QString &html, const QByteArray &snapshotKey = QByteArray());
    void showHtmlImagePayload(const QString &html);
    QUrl extractHtmlImageUrl(const QString &html) const;
    void loadHtmlImagePreview(const QUrl &url);
    void cancelHtmlImagePreview();
    void showImage(const QPixmap &pixmap, const QSize &sourceSize = QSize(), bool allowPixmapSizeFallback = true);
    void showText(const QString &text, const ClipboardItem &item);
    void showColor(const QColor &color, const QString &rawStr);
    void showUrls(const QList<QUrl> &urls, const ClipboardItem &item);
    void showWebLink(const QUrl &url, const ClipboardItem &item);
    void showFile(const QUrl &url);
    void showFiles(const QList<QUrl> &fileUrls);

    void initTextBrowser();
    void initPlainTextLabel();
    void initImageLabel();
    void initFileThumbWidget();
    void initWebLinkThumbWidget();

    bool checkWebLink(const QString &str);

    Ui::ClipboardItemInnerWidget *ui;
    QColor bgColor;
    QColor topBgColor;

    MTextBrowser *textBrowser;
    QLabel *plainTextLabel;
    QLabel *imageLabel;
    QHBoxLayout *mLayout;
    int borderWidth;
    FileThumbWidget *fileThumbWidget;
    WebLinkThumbWidget *webLinkThumbWidget;
    QNetworkAccessManager *htmlImagePreviewManager = nullptr;
    QNetworkReply *htmlImagePreviewReply = nullptr;
    QString htmlImagePreviewUrl_;
    QString pendingHtmlImageHtml_;
    ClipboardItem currentItem_;

    QColor borderColor;
    bool favoriteHighlight{false};
};

#endif // CLIPBOARDITEMINNERWIDGET_H
