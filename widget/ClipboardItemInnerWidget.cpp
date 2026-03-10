// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 ClipboardItemInnerWidget 的实现逻辑。
// pos: widget 层中的 ClipboardItemInnerWidget 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include "ClipboardItemInnerWidget.h"
#include "ui_ClipboardItemInnerWidget.h"
#include "utils/MPasteSettings.h"
#include <QPlainTextEdit>
#include <QFileInfo>
#include <QRegularExpression>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QPaintEvent>
#include <QPainter>
#include <QLinearGradient>

namespace {
QColor blendColor(const QColor &from, const QColor &to, qreal factor) {
    const qreal t = qBound(0.0, factor, 1.0);
    return QColor(
        qRound(from.red() + (to.red() - from.red()) * t),
        qRound(from.green() + (to.green() - from.green()) * t),
        qRound(from.blue() + (to.blue() - from.blue()) * t),
        qRound(from.alpha() + (to.alpha() - from.alpha()) * t)
    );
}

QPixmap scaleToFillHeightCropWidth(const QPixmap &pixmap, const QSize &targetSize, qreal devicePixelRatio) {
    if (pixmap.isNull() || !targetSize.isValid()) {
        return pixmap;
    }

    const QSize pixelTargetSize = targetSize * devicePixelRatio;
    if (!pixelTargetSize.isValid()) {
        return pixmap;
    }

    QPixmap scaled = pixmap.scaledToHeight(pixelTargetSize.height(), Qt::SmoothTransformation);
    if (scaled.width() > pixelTargetSize.width()) {
        const int x = (scaled.width() - pixelTargetSize.width()) / 2;
        scaled = scaled.copy(x, 0, pixelTargetSize.width(), pixelTargetSize.height());
    }

    scaled.setDevicePixelRatio(devicePixelRatio);
    return scaled;
}
}


QList<QUrl> buildHtmlImageFetchCandidates(const QUrl &url) {
    QList<QUrl> candidates;
    if (!url.isValid()) {
        return candidates;
    }

    candidates << url;
    if (url.host().contains(QStringLiteral("kdocs.cn"))) {
        QUrl alternate = url;
        if (url.scheme() == QStringLiteral("http")) {
            alternate.setScheme(QStringLiteral("https"));
        } else if (url.scheme() == QStringLiteral("https")) {
            alternate.setScheme(QStringLiteral("http"));
        }
        if (alternate != url) {
            candidates << alternate;
        }
    }
    return candidates;
}

void prepareHtmlImageRequest(QNetworkRequest &request, const QUrl &url) {
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 MPaste/1.0"));
    request.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(8000);
    if (url.host().contains(QStringLiteral("kdocs.cn"))) {
        request.setRawHeader("Referer", "https://www.kdocs.cn/");
        request.setRawHeader("Origin", "https://www.kdocs.cn");
    }
}

QSize extractHtmlImageSize(const QString &html) {
    static const QRegularExpression widthRegex(QStringLiteral("\\bwidth\\s*=\\s*[\"\']?([0-9]+(?:\\.[0-9]+)?)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression heightRegex(QStringLiteral("\\bheight\\s*=\\s*[\"\']?([0-9]+(?:\\.[0-9]+)?)"), QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch widthMatch = widthRegex.match(html);
    const QRegularExpressionMatch heightMatch = heightRegex.match(html);
    if (!widthMatch.hasMatch() || !heightMatch.hasMatch()) {
        return {};
    }

    const int width = qRound(widthMatch.captured(1).toDouble());
    const int height = qRound(heightMatch.captured(1).toDouble());
    if (width <= 0 || height <= 0) {
        return {};
    }

    return {width, height};
}

ClipboardItemInnerWidget::ClipboardItemInnerWidget(QColor borderColor, QWidget *parent) :
    QFrame(parent),
    ui(new Ui::ClipboardItemInnerWidget),
    bgColor(Qt::white),
    topBgColor(Qt::white),
    borderWidth(0),
    borderColor(borderColor)
{
    this->textBrowser = nullptr;
    this->imageLabel = nullptr;
    this->fileThumbWidget = nullptr;
    this->webLinkThumbWidget = nullptr;
    this->htmlImagePreviewManager = nullptr;
    this->htmlImagePreviewReply = nullptr;

    ui->setupUi(this);

    int scale = MPasteSettings::getInst()->getItemScale();
    int w = 275 * scale / 100;
    int h = 300 * scale / 100;
    setFixedSize(w, h);

    // Scale top header height
    int topH = 64 * scale / 100;
    ui->widget_2->setMinimumHeight(topH);
    ui->widget_2->setMaximumHeight(topH);

    // Scale icon size
    int iconSz = 48 * scale / 100;
    ui->iconLabel->setMinimumSize(iconSz, iconSz);
    ui->iconLabel->setMaximumSize(iconSz, iconSz);

    // Scale font sizes
    auto scaleFont = [scale](QWidget *w, int basePt) {
        QFont f = w->font();
        f.setPointSize(basePt * scale / 100);
        w->setFont(f);
    };
    scaleFont(ui->typeLabel, 12);
    scaleFont(ui->timeLabel, 10);
    scaleFont(ui->shortkeyLabel, 9);
    scaleFont(ui->countLabel, 9);

    this->setObjectName("innerWidget");
    ui->infoWidget->setObjectName("infoWidget");
    this->mLayout = new QHBoxLayout(ui->bodyWidget);
    this->mLayout->setContentsMargins(0, 0, 0, 0);
    this->mLayout->setSpacing(0);

    ui->iconLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->widget_2->setAttribute(Qt::WA_TranslucentBackground);
    ui->typeLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->timeLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->infoWidget->setStyleSheet("QWidget {color: #666666;}");

    this->refreshStyleSheet();
}

ClipboardItemInnerWidget::~ClipboardItemInnerWidget()
{
    cancelHtmlImagePreview();
    delete ui;
}

void ClipboardItemInnerWidget::setIcon(const QPixmap &nIcon) {
    QPixmap icon = nIcon;
    if (icon.isNull()) {
        icon = QPixmap(":/resources/resources/unknown.svg");
    }

    const int iconSize = 32;
    icon = icon.scaled(iconSize * devicePixelRatioF(), iconSize * devicePixelRatioF(),
                      Qt::KeepAspectRatio, Qt::SmoothTransformation);
    icon.setDevicePixelRatio(devicePixelRatioF());

    if (icon.toImage().isNull() || icon.toImage().format() == QImage::Format_Invalid) {
        QImage img = icon.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        icon = QPixmap::fromImage(img);
    }

    ui->iconLabel->setPixmap(icon);

    QImage image = icon.toImage();
    const int HUE_BINS = 12;
    float hueBins[HUE_BINS] = {};
    float hueSumSin[HUE_BINS] = {};
    float hueSumCos[HUE_BINS] = {};
    float satSum[HUE_BINS] = {};
    int binCount[HUE_BINS] = {};
    int totalColorful = 0;
    int totalPixels = 0;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor color(image.pixel(x, y));
            if (color.alpha() < 10) continue;
            ++totalPixels;

            float h, s, l;
            color.getHslF(&h, &s, &l);

            if (s < 0.15f || l < 0.1f || l > 0.9f) continue;
            ++totalColorful;

            int bin = qBound(0, (int)(h * HUE_BINS), HUE_BINS - 1);
            float weight = s;
            hueBins[bin] += weight;
            hueSumSin[bin] += weight * sinf(h * 2 * (float)M_PI);
            hueSumCos[bin] += weight * cosf(h * 2 * (float)M_PI);
            satSum[bin] += s;
            binCount[bin]++;
        }
    }

    if (totalColorful > totalPixels * 0.05 && totalPixels > 0) {
        int bestBin = 0;
        for (int i = 1; i < HUE_BINS; i++) {
            if (hueBins[i] > hueBins[bestBin]) bestBin = i;
        }
        float avgHue = atan2f(hueSumSin[bestBin], hueSumCos[bestBin]) / (2 * (float)M_PI);
        if (avgHue < 0) avgHue += 1.0f;
        float avgSat = satSum[bestBin] / binCount[bestBin];

        float s = qBound(0.35f, avgSat * 1.5f, 0.75f);
        float l = 0.45f;
        this->topBgColor = QColor::fromHslF(avgHue, s, l);
    } else {
        this->topBgColor = QColor("#4A5F7A");
    }

    this->bgColor = blendColor(this->topBgColor, QColor("#FFFFFF"), 0.975);

    this->refreshStyleSheet();
}

void ClipboardItemInnerWidget::showItem(const ClipboardItem& item) {
    this->setIcon(item.getIcon());

    const QMimeData* mimeData = item.getMimeData();
    if (!mimeData) {
        return;
    }

    const QList<QUrl> normalizedUrls = item.getNormalizedUrls();
    const QString normalizedText = item.getNormalizedText();

    const ClipboardItem::ContentType contentType = item.getContentType();
    const QPixmap itemImage = item.getImage();

    if (contentType == ClipboardItem::Color) {
        this->showColor(item.getColor(), normalizedText);
    }
    else if ((contentType == ClipboardItem::File || contentType == ClipboardItem::Link)
             && !normalizedUrls.isEmpty() && !mimeData->hasHtml()) {
        bool allValidUrls = std::all_of(normalizedUrls.begin(), normalizedUrls.end(),
            [](const QUrl& url) { return url.isValid() && !url.isRelative(); });

        if (allValidUrls) {
            this->showUrls(normalizedUrls, item);
        } else if (!normalizedText.isEmpty()) {
            this->showText(normalizedText, item);
        }
    }
    else if (contentType == ClipboardItem::Image && !itemImage.isNull()) {
        this->showImage(itemImage);
    }
    else if (contentType == ClipboardItem::Image && mimeData->hasHtml()) {
        this->showHtmlImagePayload(mimeData->html());
    }
    else if (mimeData->hasHtml()) {
        QString text = normalizedText.trimmed();
        if (contentType == ClipboardItem::Link
            && !mimeData->formats().contains("Rich Text Format")
            && !text.contains("\n")
            && (text.startsWith("http://") || text.startsWith("https://"))) {
            if (this->checkWebLink(text)) {
                this->showWebLink(text, item);
            } else {
                this->showHtml(mimeData->html());
            }
        } else {
            this->showHtml(mimeData->html());
        }
    }
    else if (!itemImage.isNull()) {
        this->showImage(itemImage);
    }
    else if (!normalizedText.isEmpty()) {
        this->showText(normalizedText, item);
    }

    ui->timeLabel->setText(QLocale::system().toString(item.getTime(), QLocale::ShortFormat));
}


void ClipboardItemInnerWidget::showBorder(bool flag) {
    if (flag) {
        this->borderWidth = 3;
        this->refreshStyleSheet();
    } else {
        this->borderWidth = 0;
        this->refreshStyleSheet();
    }
}

void ClipboardItemInnerWidget::setFavoriteHighlight(bool flag) {
    if (favoriteHighlight == flag) {
        return;
    }

    favoriteHighlight = flag;
    refreshStyleSheet();
    update();
}

void ClipboardItemInnerWidget::paintEvent(QPaintEvent *event) {
    QFrame::paintEvent(event);

    if (!favoriteHighlight) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);

    const qreal radius = 12.0;
    const struct GlowLayer {
        qreal inset;
        qreal width;
        int alpha;
    } layers[] = {
        {5.5, 7.0, 34},
        {4.0, 4.8, 56},
        {2.8, 2.6, 92}
    };

    const QColor glowColor(247, 201, 93);
    for (const GlowLayer &layer : layers) {
        QPen glowPen(QColor(glowColor.red(), glowColor.green(), glowColor.blue(), layer.alpha));
        glowPen.setWidthF(layer.width);
        painter.setPen(glowPen);
        painter.drawRoundedRect(QRectF(rect()).adjusted(layer.inset, layer.inset, -layer.inset, -layer.inset),
                                radius, radius);
    }

    QLinearGradient borderGradient(rect().topLeft(), rect().bottomRight());
    borderGradient.setColorAt(0.0, QColor(255, 245, 196));
    borderGradient.setColorAt(0.45, QColor(247, 201, 93));
    borderGradient.setColorAt(1.0, QColor(255, 232, 160));

    QPen borderPen(QBrush(borderGradient), 2.0);
    painter.setPen(borderPen);
    painter.drawRoundedRect(QRectF(rect()).adjusted(3.0, 3.0, -3.0, -3.0), radius, radius);
}

void ClipboardItemInnerWidget::refreshStyleSheet() {
    QColor effectiveBorderColor = this->borderColor;
    int effectiveBorderWidth = this->borderWidth;

    if (favoriteHighlight && effectiveBorderWidth == 0) {
        effectiveBorderColor = QColor(QStringLiteral("#F4C542"));
        effectiveBorderWidth = 2;
    }

    this->setStyleSheet(this->genStyleSheetStr(this->bgColor, this->topBgColor, effectiveBorderColor, effectiveBorderWidth));
}

void ClipboardItemInnerWidget::resetPanelStyleOverrides() {
    cancelHtmlImagePreview();
    ui->bodyWidget->setStyleSheet(QString());
    ui->infoWidget->setStyleSheet(QString());
}

void ClipboardItemInnerWidget::setInfoWidgetVisible(bool visible) {
    ui->infoWidget->setVisible(visible);
}

QString ClipboardItemInnerWidget::genStyleSheetStr(QColor bgColor, QColor topColor, QColor borderColor, int borderWidth) {
    const QColor topStart = topColor.lighter(122);
    const QColor topEnd = topColor.darker(112);
    const QColor infoColor = bgColor;

    return QString("QWidget {background-color: %1; color: #000000; } "
                   "QWidget { border-radius: 8px; }"
                   "#topWidget { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %2, stop:1 %3); border-top: 1px solid rgba(255,255,255,0.18);} "
                   "#bodyWidget { background-color: %4; } "
                   "#infoWidget { background-color: %5; border-top: none; } "
                   "#topWidget { border-bottom-left-radius: 0px; border-bottom-right-radius: 0px; }  "
                   "#infoWidget { border-top-left-radius: 0px; border-top-right-radius: 0px; }  "
                   "#typeLabel, #timeLabel { color: #FFFFFF; } "
                   "#shortkeyLabel, #countLabel, #unusedLabel { color: #556270; background: transparent; border: none; padding: 2px 8px; } "
                   "QFrame#innerWidget { border-radius: 12px; border: %6px solid %7;} ")
            .arg(bgColor.name(),
                 topStart.name(),
                 topEnd.name(),
                 bgColor.name(),
                 infoColor.name(),
                 QString::number(borderWidth),
                 borderColor.name());
}


void ClipboardItemInnerWidget::setShortkeyInfo(int num) {
    ui->shortkeyLabel->setText(QString("Alt+%1").arg(num));
}

void ClipboardItemInnerWidget::clearShortkeyInfo() {
    ui->shortkeyLabel->setText("");
}

void ClipboardItemInnerWidget::showHtml(const QString &html) {
    this->initTextBrowser();
    prepareTextBrowserDocument();
    setInfoWidgetVisible(true);
    this->textBrowser->show();
    this->textBrowser->setHtml(html);
    resetPanelStyleOverrides();

    QTextDocument doc;
    doc.setHtml(html);
    QString plainText = doc.toPlainText();
    int charCount = plainText.length();

    ui->countLabel->setText(QString("%1 ").arg(charCount) + tr("Characters"));
    ui->typeLabel->setText(tr("Rich Text"));
}


QUrl ClipboardItemInnerWidget::extractHtmlImageUrl(const QString &html) const {
    static const QRegularExpression srcRegex(
        QStringLiteral("<img[^>]+src\\s*=\\s*[\\\"\']([^\\\"\']+)[\\\"\']"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = srcRegex.match(html);
    if (!match.hasMatch()) {
        return {};
    }

    QString src = match.captured(1).trimmed();
    if (src.startsWith(QStringLiteral("//"))) {
        src.prepend(QStringLiteral("https:"));
    }

    QUrl url(src);
    return url;
}

void ClipboardItemInnerWidget::cancelHtmlImagePreview() {
    htmlImagePreviewUrl_.clear();
    pendingHtmlImageHtml_.clear();
    if (htmlImagePreviewReply) {
        htmlImagePreviewReply->abort();
        htmlImagePreviewReply->deleteLater();
        htmlImagePreviewReply = nullptr;
    }
}

void ClipboardItemInnerWidget::loadHtmlImagePreview(const QUrl &url) {
    if (!url.isValid()) {
        return;
    }

    if (!htmlImagePreviewManager) {
        htmlImagePreviewManager = new QNetworkAccessManager(this);
        htmlImagePreviewManager->setProxy(QNetworkProxy::NoProxy);
    }

    cancelHtmlImagePreview();
    auto candidates = std::make_shared<QList<QUrl>>(buildHtmlImageFetchCandidates(url));
    auto startNextRequest = std::make_shared<std::function<void()>>();

    *startNextRequest = [this, candidates, startNextRequest]() {
        if (candidates->isEmpty()) {
            if (this->imageLabel) {
                this->imageLabel->setText(tr("Preview unavailable"));
            }
            if (this->textBrowser && !pendingHtmlImageHtml_.isEmpty()) {
                this->textBrowser->show();
                this->textBrowser->setHtml(pendingHtmlImageHtml_);
            }
            return;
        }

        const QUrl currentUrl = candidates->takeFirst();
        htmlImagePreviewUrl_ = currentUrl.toString();
        QNetworkRequest request(currentUrl);
        prepareHtmlImageRequest(request, currentUrl);
        htmlImagePreviewReply = htmlImagePreviewManager->get(request);

        connect(htmlImagePreviewReply, &QNetworkReply::finished, this, [this, candidates, startNextRequest]() {
            QNetworkReply *reply = htmlImagePreviewReply;
            htmlImagePreviewReply = nullptr;
            if (!reply) {
                return;
            }

            const QByteArray payload = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (ok) {
                QPixmap pixmap;
                if (pixmap.loadFromData(payload)) {
                    pendingHtmlImageHtml_.clear();
                    this->showImage(pixmap);
                    return;
                }
            }

            (*startNextRequest)();
        });
    };

    (*startNextRequest)();
}

void ClipboardItemInnerWidget::showHtmlImagePayload(const QString &html) {
    resetPanelStyleOverrides();
    setInfoWidgetVisible(true);

    const QSize imageSize = extractHtmlImageSize(html);
    if (imageSize.isValid()) {
        ui->countLabel->setText(QString("%1 x %2 ").arg(imageSize.width()).arg(imageSize.height()) + tr("Pixels"));
    } else {
        ui->countLabel->setText(QString());
    }
    ui->typeLabel->setText(tr("Image"));

    const QUrl imageUrl = extractHtmlImageUrl(html);
    if (imageUrl.isValid() && (imageUrl.scheme().startsWith(QStringLiteral("http")) || imageUrl.scheme().isEmpty())) {
        this->initImageLabel();
        this->initTextBrowser();
        pendingHtmlImageHtml_ = html;
        this->textBrowser->hide();
        this->imageLabel->setPixmap(QPixmap());
        this->imageLabel->setText(tr("Loading preview..."));
        this->imageLabel->setAlignment(Qt::AlignCenter);
        this->imageLabel->show();
        loadHtmlImagePreview(imageUrl);
        return;
    }

    this->initTextBrowser();
    prepareTextBrowserDocument();
    this->textBrowser->show();
    this->textBrowser->setHtml(html);
}

void ClipboardItemInnerWidget::showImage(const QPixmap &pixmap) {
    this->initImageLabel();
    setInfoWidgetVisible(true);
    this->imageLabel->show();
    resetPanelStyleOverrides();
    this->imageLabel->setMargin(0);

    qreal devicePixelRatio = this->imageLabel->devicePixelRatio();
    QSize targetSize = ui->bodyWidget->size();
    int scale = MPasteSettings::getInst()->getItemScale();
    const int topH = 64 * scale / 100;
    const int infoH = ui->infoWidget->isVisible() ? ui->infoWidget->sizeHint().height() : 0;
    const QSize fallbackTargetSize(width(), height() - topH - infoH);
    if (!targetSize.isValid() || targetSize.height() < fallbackTargetSize.height() / 2) {
        targetSize = fallbackTargetSize;
    }

    QPixmap scaled = scaleToFillHeightCropWidth(pixmap, targetSize, devicePixelRatio);

    this->imageLabel->setPixmap(scaled);
    ui->countLabel->setText(QString("%1 x %2 ").arg(pixmap.height()).arg(pixmap.width()) + tr("Pixels"));
    ui->typeLabel->setText(tr("Image"));
}

void ClipboardItemInnerWidget::showText(const QString &text, const ClipboardItem &item) {
    setInfoWidgetVisible(true);
    resetPanelStyleOverrides();
    QString trimStr = text.trimmed();
    QUrl url(trimStr);
    if (QColor::isValidColorName(trimStr)) {
        this->showColor(QColor(trimStr), trimStr);
    } else if (url.isValid() && (trimStr.startsWith("http://") || trimStr.startsWith("https://"))) {
        this->showWebLink(url, item);
    } else {
        this->initTextBrowser();
        prepareTextBrowserDocument();
        this->textBrowser->show();
        this->textBrowser->setPlainText(text);
        ui->countLabel->setText(QString("%1 ").arg(text.size()) + tr("Characters"));
    }
    ui->typeLabel->setText(tr("Plain Text"));
}

void ClipboardItemInnerWidget::showColor(const QColor &color, const QString &rawStr) {
    this->initImageLabel();
    setInfoWidgetVisible(true);
    resetPanelStyleOverrides();

    this->imageLabel->show();
    QColor fontColor(255 - color.red(), 255 - color.green(), 255 - color.blue());

    this->imageLabel->setStyleSheet(QString("QLabel { border-radius: 0px; background-color: %1; color: %2;}").arg(color.name(), fontColor.name()));
    ui->infoWidget->setStyleSheet(QString("QWidget {background-color: %1;}").arg(color.name()));
    ui->countLabel->setText("");
    this->imageLabel->setText(rawStr);
    this->imageLabel->setAlignment(Qt::AlignCenter);
    ui->typeLabel->setText(tr("Color"));
}

void ClipboardItemInnerWidget::showUrls(const QList<QUrl> &urls, const ClipboardItem &item) {
    setInfoWidgetVisible(true);
    resetPanelStyleOverrides();
    if (urls.size() == 1) {
        if (urls[0].isLocalFile()) {
            this->showFile(urls[0]);
        } else {
            QString urlStr = urls[0].toString();
            if (urlStr.startsWith("http://") || urlStr.startsWith("https://")) {
                this->showWebLink(urls[0], item);
            } else {
                this->showText(item.getNormalizedText(), item);
            }
            ui->countLabel->setText(QString("%1 ").arg(urlStr.size()) + tr("Characters"));
        }
    } else if (urls.size() > 1 && urls[0].isLocalFile()) {
        this->showFiles(urls);
    } else {
        this->initTextBrowser();
        prepareTextBrowserDocument();

        this->textBrowser->show();
        QString str;
        foreach (const QUrl &url, urls) {
            str += url.toString() + "\n";
        }
        this->textBrowser->setText(str);
        ui->typeLabel->setText(tr("Links"));
        ui->countLabel->setText(QString("%1 ").arg(str.size()) + tr("Characters"));
    }
}

void ClipboardItemInnerWidget::showFile(const QUrl &url) {
    this->initFileThumbWidget();
    setInfoWidgetVisible(false);
    ui->typeLabel->setText(QString("1 ") + tr("File"));
    this->fileThumbWidget->show();
    this->fileThumbWidget->showUrl(url);
}

void ClipboardItemInnerWidget::showFiles(const QList<QUrl> &fileUrls) {
    this->initFileThumbWidget();
    setInfoWidgetVisible(false);
    ui->typeLabel->setText(QString::number(fileUrls.size()) + " " + tr("Files"));
    this->fileThumbWidget->show();
    this->fileThumbWidget->showUrls(fileUrls);
}

void ClipboardItemInnerWidget::prepareTextBrowserDocument() {
    if (!this->textBrowser) {
        return;
    }

    // Reset default block spacing from pasted HTML so the bottom edge stays tighter.
    this->textBrowser->document()->setDefaultStyleSheet(
        QStringLiteral("body, p, div, ul, ol, li { margin: 0; padding: 0; }"));
}

void ClipboardItemInnerWidget::initTextBrowser() {
    if (this->textBrowser != nullptr) return;

    this->textBrowser = new MTextBrowser(ui->bodyWidget);
    this->textBrowser->setStyleSheet(
        "QTextBrowser { border-radius: 0px; padding: 6px 10px 2px 10px; }");
    this->textBrowser->setFrameStyle(QFrame::NoFrame);
    this->textBrowser->setReadOnly(true);
    this->textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->textBrowser->setContentsMargins(0, 0, 0, 0);
    this->textBrowser->viewport()->setContentsMargins(0, 0, 0, 0);
    this->textBrowser->document()->setDocumentMargin(0);
    this->textBrowser->setWordWrapMode(QTextOption::WordWrap);
    this->textBrowser->setAttribute(Qt::WA_TranslucentBackground);
    this->textBrowser->setDisabled(true);
    this->textBrowser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->mLayout->addWidget(this->textBrowser);
    this->textBrowser->hide();
}

void ClipboardItemInnerWidget::initImageLabel() {
    if (this->imageLabel != nullptr) return;

    this->imageLabel = new QLabel(ui->bodyWidget);
    this->imageLabel->hide();
    this->imageLabel->setAlignment(Qt::AlignCenter);
    this->imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->imageLabel->setStyleSheet("QWidget { border-radius: 0px; } ");
    this->mLayout->addWidget(this->imageLabel);
}

void ClipboardItemInnerWidget::initFileThumbWidget() {
    if (this->fileThumbWidget != nullptr) return;

    this->fileThumbWidget = new FileThumbWidget(ui->bodyWidget);
    this->fileThumbWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->fileThumbWidget->setStyleSheet("QWidget { border-radius: 0px; } ");
    this->fileThumbWidget->hide();
    this->mLayout->addWidget(this->fileThumbWidget);
}

void ClipboardItemInnerWidget::initWebLinkThumbWidget() {
    if (this->webLinkThumbWidget != nullptr) return;

    this->webLinkThumbWidget = new WebLinkThumbWidget(ui->bodyWidget);
    this->webLinkThumbWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->webLinkThumbWidget->setStyleSheet("QWidget { border-radius: 0px; } ");
    this->webLinkThumbWidget->hide();
    this->mLayout->addWidget(this->webLinkThumbWidget);

    connect(this->webLinkThumbWidget, &WebLinkThumbWidget::itemNeedToSave, [this] (const ClipboardItem &item) {
        Q_EMIT itemNeedToSave(item);
    });
}

void ClipboardItemInnerWidget::showWebLink(const QUrl &url, const ClipboardItem &item) {
    this->initWebLinkThumbWidget();
    resetPanelStyleOverrides();
    setInfoWidgetVisible(false);
    this->webLinkThumbWidget->show();
    this->webLinkThumbWidget->showWebLink(url, item);
    ui->typeLabel->setText(tr("Link"));
}

bool ClipboardItemInnerWidget::checkWebLink(const QString &str) {
    QString trimStr = str.trimmed();
    QUrl url(trimStr, QUrl::StrictMode);
    return url.isValid() && (trimStr.startsWith("https://") || trimStr.startsWith("http://"));
}

//void ClipboardItemInnerWidget::refreshTimeGap() {
//    // calculate time
//    qlonglong sec = item.getTime().secsTo(QDateTime::currentDateTime());
//    QString timStr;
//    if (sec < 60) {
//        timStr = QString("%1 seconds ago").arg(sec);
//    } else if (sec < 3600) {
//        timStr = QString("%1 minutes ago").arg(sec / 60);
//    } else if (sec < 60 * 60 * 24) {
//        timStr = QString("%1 hours ago").arg(sec / (60 * 60));
//    } else if (sec < 60 * 60 * 24 * 7) {
//        timStr = QString("%1 days ago").arg(sec / (60 * 60 * 24));
//    } else if (sec < 60 * 60 * 24 * 30) {
//        timStr = QString("%1 weeks ago").arg(sec / (60 * 60 * 24 * 7));
//    } else {
//        timStr = QString("%1 months ago").arg(sec / (60 * 60 * 24 * 30));
//    }
//    ui->timeLabel->setText(timStr);
//}
