#ifndef WEBLINKTHUMBWIDGET_H
#define WEBLINKTHUMBWIDGET_H

#include <QWidget>
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
    void setPreview(const OpenGraphItem &ogItem);
    void showItem(const ClipboardItem &item);

private:
    Ui::WebLinkThumbWidget *ui;
    ClipboardItem item;
    QUrl url;
    OpenGraphFetcher *ogFetcher;
};

#endif // WEBLINKTHUMBWIDGET_H
