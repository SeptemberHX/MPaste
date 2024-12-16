#include <iostream>
#include "WebLinkThumbWidget.h"
#include "ui_WebLinkThumbWidget.h"

WebLinkThumbWidget::WebLinkThumbWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WebLinkThumbWidget)
{
    ui->setupUi(this);
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

    QPixmap unknow(":/resources/resources/unknown_favico.svg");
    unknow.setDevicePixelRatio(this->devicePixelRatioF());
    ui->imageLabel->setPixmap(unknow.scaled(ui->imageLabel->width() * this->devicePixelRatio(), 160 * this->devicePixelRatio(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

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
        QPixmap tPixmap = item.getFavicon();
        tPixmap.setDevicePixelRatio(this->devicePixelRatioF());

        // 预览图尽可能保持原始分辨率
        const int previewWidth = ui->imageLabel->width();
        const int previewHeight = ui->imageLabel->height();

        QSize imageSize = tPixmap.size();
        QSize targetSize(previewWidth * this->devicePixelRatio(),
                        previewHeight * this->devicePixelRatio());

        // 如果图片太大则等比缩小，但尽量保持高分辨率
        if (imageSize.width() > targetSize.width() ||
            imageSize.height() > targetSize.height()) {
            ui->imageLabel->setPixmap(tPixmap.scaled(
                targetSize,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            ));
            } else {
                // 如果图片较小，则适当放大但不超过目标尺寸
                if (imageSize.width() < previewWidth / 2 ||
                    imageSize.height() < previewHeight / 2) {
                    ui->imageLabel->setPixmap(tPixmap.scaled(
                        targetSize,
                        Qt::KeepAspectRatio,
                        Qt::SmoothTransformation
                    ));
                    } else {
                        // 原始尺寸合适则直接使用
                        ui->imageLabel->setPixmap(tPixmap);
                    }
            }
    }

    QFontMetrics fm(ui->titleLabel->font());
    QString elidedStr = item.getTitle();
    if (fm.boundingRect(item.getTitle()).width() > ui->titleLabel->width()) {
        ui->titleLabel->setAlignment(Qt::AlignLeft);
        elidedStr = fm.elidedText(item.getTitle(), Qt::ElideRight, ui->titleLabel->width());
    }
    ui->titleLabel->setText(elidedStr);
    ui->titleLabel->setStyleSheet("QWidget { color: #555555; }");
    ui->urlLabel->setStyleSheet("QWidget { color: #555555; }");
}
