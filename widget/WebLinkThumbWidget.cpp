// input: Depends on WebLinkThumbWidget.h, OpenGraph metadata, and image scaling rules for link previews.
// output: Implements a link preview widget whose main image fills the preview area while staying centered, with lighter text insets.
// pos: Widget-layer link preview implementation embedded inside clipboard item cards.
// update: If I change, update this header block and my folder README.md.
#include <QFontMetrics>
#include "WebLinkThumbWidget.h"
#include "ui_WebLinkThumbWidget.h"
#include "utils/MPasteSettings.h"

namespace {
    constexpr int DEFAULT_IMAGE_HEIGHT = 160;
    const QString DEFAULT_STYLE = "QWidget { color: #555555; }";
}

WebLinkThumbWidget::WebLinkThumbWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WebLinkThumbWidget)
{
    ui->setupUi(this);

    int scale = MPasteSettings::getInst()->getItemScale();

    // Scale titleLabel height
    int titleH = 20 * scale / 100;
    ui->titleLabel->setMinimumHeight(titleH);
    ui->titleLabel->setMaximumHeight(titleH);

    // Scale fonts
    auto scaleFont = [scale](QWidget *w, int basePt) {
        QFont f = w->font();
        f.setPointSize(basePt * scale / 100);
        w->setFont(f);
    };
    scaleFont(ui->titleLabel, 10);
    scaleFont(ui->urlLabel, 10);

    ui->titleLabel->setIndent(8);
    ui->titleLabel->setContentsMargins(8, 0, 8, 0);
    ui->urlLabel->setContentsMargins(8, 0, 8, 8);
    ui->urlLabel->setMargin(0);

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
    QSize targetSize(previewWidth * devicePixelRatio(),
                    previewHeight * devicePixelRatio());

    QPixmap scaled = processedPixmap.scaled(
        targetSize,
        Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation
    );

    scaled.setDevicePixelRatio(devicePixelRatioF());
    return scaled;
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
    ui->imageLabel->setMargin(0);
}
