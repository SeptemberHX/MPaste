#include <QFontMetrics>
#include "WebLinkThumbWidget.h"
#include "ui_WebLinkThumbWidget.h"

namespace {
    constexpr int DEFAULT_IMAGE_HEIGHT = 160;
    const QString DEFAULT_STYLE = "QWidget { color: #555555; }";
}

WebLinkThumbWidget::WebLinkThumbWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WebLinkThumbWidget)
{
    ui->setupUi(this);
    setDefaultStyle();
}

WebLinkThumbWidget::~WebLinkThumbWidget() = default;

void WebLinkThumbWidget::showWebLink(const QUrl &url, const ClipboardItem &item) {
    currentItem = item;
    currentUrl = url;

    setupInitialPreview(url);

    if (item.getUrl().isEmpty()) {
        ogFetcher.reset(new OpenGraphFetcher(url));
        connect(ogFetcher.data(), &OpenGraphFetcher::finished,
                this, &WebLinkThumbWidget::handleOpenGraphData);
        ogFetcher->handle();
    } else {
        updatePreviewFromItem(item);
    }
}

void WebLinkThumbWidget::setupInitialPreview(const QUrl &url) {
    // Set initial URL
    setElidedText(ui->urlLabel, url.toString());

    // Set default image
    QPixmap defaultImage(":/resources/resources/unknown_favico.svg");
    defaultImage.setDevicePixelRatio(devicePixelRatioF());
    ui->imageLabel->setPixmap(processImage(defaultImage));
}

void WebLinkThumbWidget::handleOpenGraphData(const OpenGraphItem &ogItem) {
    if (!ogItem.getImage().isNull()) {
        currentItem.setFavicon(ogItem.getImage());
    }

    currentItem.setTitle(ogItem.getTitle());
    currentItem.setUrl(currentUrl.toString());

    updatePreviewFromItem(currentItem);
    Q_EMIT itemNeedToSave(currentItem);
}

void WebLinkThumbWidget::updatePreviewFromItem(const ClipboardItem &item) {
    if (!item.getFavicon().isNull()) {
        ui->imageLabel->setPixmap(processImage(item.getFavicon()));
    }

    setElidedText(ui->titleLabel, item.getTitle());
    setElidedText(ui->urlLabel, item.getUrl());
}

QPixmap WebLinkThumbWidget::processImage(const QPixmap &originalPixmap) const {
    QPixmap processedPixmap = originalPixmap;
    processedPixmap.setDevicePixelRatio(devicePixelRatioF());

    const int previewWidth = ui->imageLabel->width();
    const int previewHeight = ui->imageLabel->height();

    QSize imageSize = processedPixmap.size();
    QSize targetSize(previewWidth * devicePixelRatio(),
                    previewHeight * devicePixelRatio());

    // Scale image only if necessary
    if (imageSize.width() > targetSize.width() ||
        imageSize.height() > targetSize.height() ||
        imageSize.width() < previewWidth / 2 ||
        imageSize.height() < previewHeight / 2) {

        return processedPixmap.scaled(
            targetSize,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
    }

    return processedPixmap;
}

void WebLinkThumbWidget::setElidedText(QLabel *label, const QString &text) {
    if (!label || text.isEmpty()) return;

    QFontMetrics fm(label->font());
    QString elidedText = text;

    if (fm.boundingRect(text).width() > label->width()) {
        label->setAlignment(Qt::AlignLeft);
        elidedText = fm.elidedText(text, Qt::ElideRight, label->width());
    } else {
        label->setAlignment(Qt::AlignCenter);
    }

    label->setText(elidedText);
}

void WebLinkThumbWidget::setDefaultStyle() {
    ui->titleLabel->setStyleSheet(DEFAULT_STYLE);
    ui->urlLabel->setStyleSheet(DEFAULT_STYLE);
}