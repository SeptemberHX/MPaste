#include <iostream>
#include "WebLinkThumbWidget.h"
#include "ui_WebLinkThumbWidget.h"

WebLinkThumbWidget::WebLinkThumbWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WebLinkThumbWidget)
{
    ui->setupUi(this);
    ui->imageLabel->setFixedHeight(200);
}

WebLinkThumbWidget::~WebLinkThumbWidget()
{
    delete ui;
}

void WebLinkThumbWidget::showWebLink(const QUrl &url, const ClipboardItem &item) {
    this->item = item;
    QString urlStr = url.toString();
    QFontMetrics fm(ui->urlLabel->font());
    urlStr = fm.elidedText(urlStr, Qt::ElideRight, ui->urlLabel->width());
    ui->urlLabel->setText(urlStr);

    ui->imageLabel->setPixmap(QPixmap(":/resources/resources/unknown_favico.svg").scaled(ui->imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (item.getUrl().isEmpty()) {
        this->ogFetcher = new OpenGraphFetcher(url);
        this->url = url;
        connect(this->ogFetcher, &OpenGraphFetcher::finished, this, &WebLinkThumbWidget::setPreview);
        this->ogFetcher->handle();
    }  else {
        this->showItem(this->item);
    }
}

void WebLinkThumbWidget::setPreview(const OpenGraphItem &ogItem) {
    if (!ogItem.getImage().isNull()) {
        this->item.setFavicon(ogItem.getImage());
    }

    this->item.setTitle(ogItem.getTitle());
    this->item.setUrl(this->url.toString());
    this->showItem(this->item);
    Q_EMIT itemNeedToSave(this->item);
}

void WebLinkThumbWidget::showItem(const ClipboardItem &item) {
    if (!item.getFavicon().isNull()) {
        ui->imageLabel->setPixmap(item.getFavicon().scaled(ui->imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QFontMetrics fm(ui->urlLabel->font());
    QString elidedStr = fm.elidedText(item.getTitle(), Qt::ElideRight, ui->titleLabel->width());
    ui->titleLabel->setText(elidedStr);
    ui->titleLabel->setStyleSheet("QWidget { color: #555555; }");
    ui->urlLabel->setStyleSheet("QWidget { color: #555555; }");
}
