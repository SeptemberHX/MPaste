// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 WebLinkThumbWidget 的声明接口。
// pos: widget 层中的 WebLinkThumbWidget 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#ifndef WEBLINKTHUMBWIDGET_H
#define WEBLINKTHUMBWIDGET_H

#include <QWidget>
#include <QScopedPointer>
#include <QLabel>
#include <data/ClipboardItem.h>
#include "utils/OpenGraphFetcher.h"

namespace Ui {
    class WebLinkThumbWidget;
}

class WebLinkThumbWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WebLinkThumbWidget(QWidget *parent = nullptr);
    void showWebLink(const QUrl &url, const ClipboardItem &item);
    ~WebLinkThumbWidget();

    signals:
        void itemNeedToSave(const ClipboardItem &item);

    private slots:
        void handleOpenGraphData(const OpenGraphItem &ogItem);

private:
    void setupInitialPreview(const QUrl &url);
    void updatePreviewFromItem(const ClipboardItem &item);
    void setElidedText(QLabel *label, const QString &text);
    QPixmap processImage(const QPixmap &originalPixmap) const;
    void setDefaultStyle();

private:
    QScopedPointer<Ui::WebLinkThumbWidget> ui;
    QScopedPointer<OpenGraphFetcher> ogFetcher;
    ClipboardItem currentItem;
    QUrl currentUrl;
};

#endif // WEBLINKTHUMBWIDGET_H
