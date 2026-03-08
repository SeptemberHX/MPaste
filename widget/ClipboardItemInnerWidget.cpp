// input: 依赖对应头文件、Qt 运行时与资源/服务组件。
// output: 对外提供 ClipboardItemInnerWidget 的实现行为。
// pos: widget 层中的 ClipboardItemInnerWidget 实现文件。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include "ClipboardItemInnerWidget.h"
#include "ui_ClipboardItemInnerWidget.h"
#include "utils/MPasteSettings.h"
#include <QPlainTextEdit>
#include <QFileInfo>

ClipboardItemInnerWidget::ClipboardItemInnerWidget(QColor borderColor, QWidget *parent) :
    QFrame(parent),
    borderColor(borderColor),
    ui(new Ui::ClipboardItemInnerWidget),
    bgColor(Qt::white),
    topBgColor(Qt::white),
    borderWidth(0)
{
    this->textBrowser = nullptr;
    this->imageLabel = nullptr;
    this->fileThumbWidget = nullptr;
    this->webLinkThumbWidget = nullptr;

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
    // 新代码 (Qt6)
    this->mLayout->setContentsMargins(0, 0, 0, 0);  // 左、上、右、下都设为0

    ui->iconLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->widget_2->setAttribute(Qt::WA_TranslucentBackground);
    ui->typeLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->timeLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->infoWidget->setStyleSheet("QWidget {color: #666666;}");

    this->refreshStyleSheet();
}

ClipboardItemInnerWidget::~ClipboardItemInnerWidget()
{
    delete ui;
}

void ClipboardItemInnerWidget::setIcon(const QPixmap &nIcon) {
    QPixmap icon = nIcon;
    if (icon.isNull()) {
        icon = QPixmap(":/resources/resources/unknown.svg");
    }

    // 确保图标大小合适
    const int iconSize = 32;  // 或者其他合适的大小
    icon = icon.scaled(iconSize * devicePixelRatioF(), iconSize * devicePixelRatioF(),
                      Qt::KeepAspectRatio, Qt::SmoothTransformation);
    icon.setDevicePixelRatio(devicePixelRatioF());

    // 如果图标完全透明，尝试转换为 ARGB32 格式
    if (icon.toImage().isNull() || icon.toImage().format() == QImage::Format_Invalid) {
        QImage img = icon.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        icon = QPixmap::fromImage(img);
    }

    ui->iconLabel->setPixmap(icon);

    // 提取主色调：基于色相直方图找到图标的主导颜色
    QImage image = icon.toImage();
    const int HUE_BINS = 12;
    float hueBins[HUE_BINS] = {};      // 每个色相区间的加权计数
    float hueSumSin[HUE_BINS] = {};    // 用于计算区间内精确平均色相
    float hueSumCos[HUE_BINS] = {};
    float satSum[HUE_BINS] = {};       // 区间内饱和度总和
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

            // 只统计有色彩的像素（过滤灰色、纯白、纯黑）
            if (s < 0.15f || l < 0.1f || l > 0.9f) continue;
            ++totalColorful;

            int bin = qBound(0, (int)(h * HUE_BINS), HUE_BINS - 1);
            float weight = s;  // 饱和度越高权重越大
            hueBins[bin] += weight;
            hueSumSin[bin] += weight * sinf(h * 2 * (float)M_PI);
            hueSumCos[bin] += weight * cosf(h * 2 * (float)M_PI);
            satSum[bin] += s;
            binCount[bin]++;
        }
    }

    if (totalColorful > totalPixels * 0.05 && totalPixels > 0) {
        // 找到权重最高的色相区间
        int bestBin = 0;
        for (int i = 1; i < HUE_BINS; i++) {
            if (hueBins[i] > hueBins[bestBin]) bestBin = i;
        }
        // 计算区间内的精确平均色相
        float avgHue = atan2f(hueSumSin[bestBin], hueSumCos[bestBin]) / (2 * (float)M_PI);
        if (avgHue < 0) avgHue += 1.0f;
        float avgSat = satSum[bestBin] / binCount[bestBin];

        // 提高饱和度和降低亮度，增强不同图标之间的区分度
        float s = qBound(0.35f, avgSat * 1.5f, 0.75f);
        float l = 0.45f;
        this->topBgColor = QColor::fromHslF(avgHue, s, l, 0.0);
    } else {
        this->topBgColor = QColor::fromHslF(0, 0, 0.55f, 0.0);
    }

    this->refreshStyleSheet();
}

void ClipboardItemInnerWidget::showItem(const ClipboardItem& item) {
    this->setIcon(item.getIcon());

    const QMimeData* mimeData = item.getMimeData();
    if (!mimeData) {
        return;
    }

    // 从高优先级到低优先级判断
    if (mimeData->hasColor()) {
        // 颜色数据
        this->showColor(item.getColor(), item.getText());
    }
    else if (mimeData->hasUrls() && !mimeData->hasHtml()) {
        // 确保是纯URL，而不是富文本或代码中包含的URL
        QList<QUrl> urls = mimeData->urls();
        bool allValidUrls = !urls.isEmpty() && std::all_of(urls.begin(), urls.end(),
            [](const QUrl& url) { return url.isValid() && !url.isRelative(); });

        if (allValidUrls) {
            this->showUrls(urls, item);
        } else if (mimeData->hasText()) {
            this->showText(mimeData->text(), item);
        }
    }
    else if (mimeData->hasHtml()) {
        QString text = mimeData->text().trimmed();
        // 检查是否是真正的网页链接
        if (!mimeData->formats().contains("Rich Text Format") &&
            !text.contains("\n") &&
            (text.startsWith("http://") || text.startsWith("https://"))) {
            if (this->checkWebLink(text)) {
                this->showWebLink(text, item);
            } else {
                this->showHtml(mimeData->html());
            }
            } else {
                // IDE代码或其他富文本内容
                this->showHtml(mimeData->html());
            }
    }
    else if (mimeData->hasImage()) {
        // 图片数据优先于其他格式
        this->showImage(item.getImage());
    }
    else if (mimeData->hasText()) {
        // 纯文本内容
        this->showText(mimeData->text(), item);
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

void ClipboardItemInnerWidget::refreshStyleSheet() {
    this->setStyleSheet(this->genStyleSheetStr(this->bgColor, this->topBgColor, this->borderColor, this->borderWidth));
}

QString ClipboardItemInnerWidget::genStyleSheetStr(QColor bgColor, QColor topColor, QColor borderColor, int borderWidth) {
    return QString("QWidget {background-color: %1; color: #000000; } "
                   "QWidget { border-radius: 8px; }"
                   "#topWidget { background-color: %2;} "
                   "#topWidget { border-bottom-left-radius: 0px; border-bottom-right-radius: 0px; }  "
                   "#infoWidget { border-top-left-radius: 0px; border-top-right-radius: 0px; }  "
                   "#typeLabel, #timeLabel { color: #FFFFFF; } "
                   "QFrame#innerWidget { border-radius: 12px; border: %3px solid %4;} ").arg(bgColor.name(), topColor.name(), QString::number(borderWidth), borderColor.name());
}

void ClipboardItemInnerWidget::setShortkeyInfo(int num) {
    ui->shortkeyLabel->setText(QString("Alt+%1").arg(num));
}

void ClipboardItemInnerWidget::clearShortkeyInfo() {
    ui->shortkeyLabel->setText("");
}

void ClipboardItemInnerWidget::showHtml(const QString &html) {
    this->initTextBrowser();
    this->textBrowser->show();
    this->textBrowser->setHtml(html);

    QRegularExpression bgColorExp("background-color:(#[A-Za-z0-9]{6})");
    QRegularExpressionMatch match = bgColorExp.match(html.trimmed());

    if (match.hasMatch()) {
        QString colorStr = match.captured(1);
        ui->bodyWidget->setStyleSheet(QString("#bodyWidget {background-color:%1; border-radius: 0px; }").arg(colorStr));
        ui->infoWidget->setStyleSheet(QString("QWidget {background-color:%1; color: #666666;}").arg(colorStr));
    } else {
        bgColorExp = QRegularExpression(R"(background.*:[ ]*rgb\((\d*),[ ]*(\d*),[ ]*(\d*)\))");
        match = bgColorExp.match(html.trimmed());
        if (match.hasMatch()) {
            int r = match.captured(1).toInt();
            int g = match.captured(2).toInt();
            int b = match.captured(3).toInt();
            QColor colorStr(r, g, b);
            ui->bodyWidget->setStyleSheet(QString("#bodyWidget {background-color:%1;  border-radius: 0px;}").arg(colorStr.name()));
            ui->infoWidget->setStyleSheet(QString("QWidget {background-color:%1; color: #666666;}").arg(colorStr.name()));
        }
    }

    // 使用 QTextDocument 来提取纯文本内容
    QTextDocument doc;
    doc.setHtml(html);
    QString plainText = doc.toPlainText();
    int charCount = plainText.length();

    ui->countLabel->setText(QString("%1 ").arg(charCount) + tr("Characters"));
    ui->typeLabel->setText(tr("Rich Text"));
}

void ClipboardItemInnerWidget::showImage(const QPixmap &pixmap) {
    this->initImageLabel();
    this->imageLabel->show();
    this->imageLabel->setMargin(10);

    // 获取设备像素比
    qreal devicePixelRatio = this->imageLabel->devicePixelRatio();
    // 根据 DPI 调整目标尺寸, using dynamic card size
    int scale = MPasteSettings::getInst()->getItemScale();
    int topH = 64 * scale / 100;
    int targetWidth = width() * devicePixelRatio;
    int targetHeight = (height() - topH - 2) * devicePixelRatio;

    // 缩放时考虑设备像素比
    QPixmap scaled = pixmap.scaled(
        targetWidth,
        targetHeight,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    );
    // 设置缩放后图片的设备像素比
    scaled.setDevicePixelRatio(devicePixelRatio);

    this->imageLabel->setPixmap(scaled);
    ui->countLabel->setText(QString("%1 x %2 ").arg(pixmap.height()).arg(pixmap.width()) + tr("Pixels"));
    ui->typeLabel->setText(tr("Image"));
}

void ClipboardItemInnerWidget::showText(const QString &text, const ClipboardItem &item) {
    QString trimStr = text.trimmed();
    QUrl url(trimStr);
    if (QColor::isValidColor(trimStr)) {
        this->showColor(QColor(trimStr), trimStr);
    } else if (url.isValid() && (trimStr.startsWith("http://") || trimStr.startsWith("https://"))) {
        this->showWebLink(url, item);
    } else {
        this->initTextBrowser();
        this->textBrowser->show();
        this->textBrowser->setPlainText(text);
        ui->countLabel->setText(QString("%1 ").arg(text.size()) + tr("Characters"));
    }
    ui->typeLabel->setText(tr("Plain Text"));
}

void ClipboardItemInnerWidget::showColor(const QColor &color, const QString &rawStr) {
    this->initImageLabel();

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
    if (urls.size() == 1) {
        if (urls[0].isLocalFile()) {
            this->showFile(urls[0]);
        } else {
            QString urlStr = urls[0].toString();
            if (urlStr.startsWith("http://") || urlStr.startsWith("https://")) {
                this->showWebLink(urls[0], item);
            } else {
                this->showText(item.getText(), item);
            }
            ui->countLabel->setText(QString("%1 ").arg(urlStr.size()) + tr("Characters"));
        }
    } else if (urls.size() > 1 && urls[0].isLocalFile()) {
        this->showFiles(urls);
    } else {
        this->initTextBrowser();

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
    ui->typeLabel->setText(QString("1 ") + tr("File"));
    this->fileThumbWidget->show();
    this->fileThumbWidget->showUrl(url);
}

void ClipboardItemInnerWidget::showFiles(const QList<QUrl> &fileUrls) {
    this->initFileThumbWidget();
    ui->typeLabel->setText(QString::number(fileUrls.size()) + " " + tr("Files"));
    this->fileThumbWidget->show();
    this->fileThumbWidget->showUrls(fileUrls);
}

void ClipboardItemInnerWidget::initTextBrowser() {
    if (this->textBrowser != nullptr) return;

    this->textBrowser = new MTextBrowser(ui->bodyWidget);
    this->textBrowser->setStyleSheet("QTextBrowser { border-radius: 0px; padding: 10px; }");
    this->textBrowser->setFrameStyle(QFrame::NoFrame);
    this->textBrowser->setReadOnly(true);
    this->textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->textBrowser->setContentsMargins(0, 0, 0, 0);
    this->textBrowser->document()->setDocumentMargin(0);
    this->textBrowser->setWordWrapMode(QTextOption::WordWrap);
    this->textBrowser->setAttribute(Qt::WA_TranslucentBackground);
    this->textBrowser->setDisabled(true);
    this->mLayout->addWidget(this->textBrowser);
    this->textBrowser->hide();
}

void ClipboardItemInnerWidget::initImageLabel() {
    if (this->imageLabel != nullptr) return;

    this->imageLabel = new QLabel(ui->bodyWidget);
    this->imageLabel->hide();
    this->imageLabel->setAlignment(Qt::AlignCenter);
    this->imageLabel->setStyleSheet("QWidget { border-radius: 0px; } ");
    this->mLayout->addWidget(this->imageLabel);
}

void ClipboardItemInnerWidget::initFileThumbWidget() {
    if (this->fileThumbWidget != nullptr) return;

    this->fileThumbWidget = new FileThumbWidget(ui->bodyWidget);
    this->fileThumbWidget->setStyleSheet("QWidget { border-radius: 0px; } ");
    this->fileThumbWidget->hide();
    this->mLayout->addWidget(this->fileThumbWidget);
}

void ClipboardItemInnerWidget::initWebLinkThumbWidget() {
    if (this->webLinkThumbWidget != nullptr) return;

    this->webLinkThumbWidget = new WebLinkThumbWidget(ui->bodyWidget);
    this->webLinkThumbWidget->setStyleSheet("QWidget { border-radius: 0px; } ");
    this->webLinkThumbWidget->hide();
    this->mLayout->addWidget(this->webLinkThumbWidget);

    connect(this->webLinkThumbWidget, &WebLinkThumbWidget::itemNeedToSave, [this] (const ClipboardItem &item) {
        Q_EMIT itemNeedToSave(item);
    });
}

void ClipboardItemInnerWidget::showWebLink(const QUrl &url, const ClipboardItem &item) {
    this->initWebLinkThumbWidget();
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
