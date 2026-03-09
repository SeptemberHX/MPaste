// input: 濞撴碍绻嗙粋?Qt Widgets闁靛棔榫歛ta 閻忕偛鍊搁顔炬寬閳ヨ尙鐟㈤柛姘嫰閻壆绱掗崟顏咁偨濠㈠湱澧楀Σ鎴﹀Υ?
// output: 閻庣數鎳撻ˇ濠氬箵閹邦亞杩?ClipboardItemInnerWidget 闁汇劌瀚敍鎰板及鎼淬垹澶嶉柛娆欑祷閳?
// pos: widget 閻忕偛鍊烽懙鎴︽儍?ClipboardItemInnerWidget 闁规亽鍎辫ぐ娑氣偓瑙勭煯缁犵喖濡?
// update: 濞戞挴鍋撻柡鍐块檮閸ㄦ粎鎮锝嗙函闁哄倸搴滅槐婵嬪礉閳ュ磭绠戦柡鍥х摠閺屽﹪骞嬮幋鐘崇暠鐎殿喒鍋撳璺虹摠閺佺偤鏌屾繝蹇曠濞寸姰鍎卞鐑藉箥閳ь剛浠﹂悙鍨暠闁哄倸娲ｅ▎銏″緞閸︻厽鐣?README.md闁?
#ifndef CLIPBOARDITEMINNERWIDGET_H
#define CLIPBOARDITEMINNERWIDGET_H

#include <QFrame>
#include <QWidget>
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
    void setInfoWidgetVisible(bool visible);
    void showHtml(const QString &html);
    void showHtmlImagePayload(const QString &html);
    QUrl extractHtmlImageUrl(const QString &html) const;
    void loadHtmlImagePreview(const QUrl &url);
    void cancelHtmlImagePreview();
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
    QNetworkAccessManager *htmlImagePreviewManager = nullptr;
    QNetworkReply *htmlImagePreviewReply = nullptr;
    QString htmlImagePreviewUrl_;
    QString pendingHtmlImageHtml_;

    QColor borderColor;
};

#endif // CLIPBOARDITEMINNERWIDGET_H
