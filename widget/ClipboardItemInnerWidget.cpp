#include <iostream>
#include "ClipboardItemInnerWidget.h"
#include "ui_ClipboardItemInnerWidget.h"
#include <QPlainTextEdit>

ClipboardItemInnerWidget::ClipboardItemInnerWidget(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::ClipboardItemInnerWidget),
    bgColor(Qt::white),
    topBgColor(Qt::white),
    borderWidth(0)
{
    ui->setupUi(this);
    this->setObjectName("innerWidget");
    this->mLayout = new QHBoxLayout(ui->bodyWidget);
    this->mLayout->setMargin(0);

    this->textBrowser = new MTextBrowser(ui->bodyWidget);
    this->textBrowser->setFrameStyle(QFrame::NoFrame);
    this->textBrowser->setReadOnly(true);
    this->textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->textBrowser->setContentsMargins(0, 0, 0, 0);
    this->textBrowser->document()->setDocumentMargin(5);
    this->textBrowser->setAttribute(Qt::WA_TranslucentBackground);
    this->mLayout->addWidget(this->textBrowser);
    this->textBrowser->hide();

    this->imageLabel = new QLabel(ui->bodyWidget);
    this->imageLabel->hide();
    this->imageLabel->setAlignment(Qt::AlignCenter);
    this->mLayout->addWidget(this->imageLabel);

    ui->iconLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->widget_2->setAttribute(Qt::WA_TranslucentBackground);
    ui->typeLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->timeLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->infoWidget->setStyleSheet("{color: #666666;}");

    this->refreshStyleSheet();
}

ClipboardItemInnerWidget::~ClipboardItemInnerWidget()
{
    delete ui;
}

void ClipboardItemInnerWidget::setIcon(const QPixmap &icon) {
    ui->iconLabel->setPixmap(icon.scaled(ui->iconLabel->sizeHint(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    int r = 0, g = 0, b = 0, a = 0, n = 0;
    QImage image = icon.toImage();
    for (int row = 1; row < image.height(); ++row) {
        for (int c = 1; c < image.width(); ++c) {
            QColor current(image.pixel(row, c));
            r += current.red();
            g += current.green();
            b += current.blue();
            a += current.alpha();
            ++n;
        }
    }

    // make it lighter
    r /= n;
    r += (255 - r) / 6;
    g /= n;
    g += (255 - g) / 6;
    b /= n;
    b += (255 - b) / 6;
    this->topBgColor = QColor(r, g, b, 0);
    this->refreshStyleSheet();
}

void ClipboardItemInnerWidget::showItem(const ClipboardItem& item) {
    this->setIcon(item.getIcon());

    if (item.getColor().isValid()) {  // actually I haven't meet this situation yet :p
        this->showColor(item.getColor());
    } else if (!item.getImage().isNull()) {
        this->showImage(item.getImage());
    } else if (!item.getHtml().isEmpty() && !QColor::isValidColor(item.getText().trimmed())) {
        this->showHtml(item.getHtml());
    } else if (!item.getUrls().isEmpty()) {
        this->showUrls(item.getUrls());
    } else if (!item.getText().isEmpty()) {
        this->showText(item.getText());
    }

    ui->timeLabel->setText(item.getTime().toString(Qt::SystemLocaleShortDate));
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
    this->setStyleSheet(this->genStyleSheetStr(this->bgColor, this->topBgColor, this->borderWidth));
}

QString ClipboardItemInnerWidget::genStyleSheetStr(QColor bgColor, QColor topColor, int borderWidth) {
    return QString("QWidget {background-color: %1; color: #000000; } "
                   "#topWidget { background-color: %2;} "
                   "#typeLabel, #timeLabel { color: #FFFFFF; } "
                   "QFrame#innerWidget {border: %3px solid #1684fc;} ").arg(bgColor.name(), topColor.name(), QString::number(borderWidth));
}

void ClipboardItemInnerWidget::setShortkeyInfo(int num) {
    ui->shortkeyLabel->setText(QString("Alt+%1").arg(num));
}

void ClipboardItemInnerWidget::clearShortkeyInfo() {
    ui->shortkeyLabel->setText("");
}

void ClipboardItemInnerWidget::showHtml(const QString &html) {
    this->textBrowser->show();
    this->textBrowser->setHtml(html);

    QRegExp bgColorExp("background-color:(#[A-Za-z0-9]{6})");
    if (bgColorExp.indexIn(html.trimmed()) != -1) {
        QString colorStr = bgColorExp.cap(1);
        ui->bodyWidget->setStyleSheet(QString("#bodyWidget {background-color:%1;}").arg(colorStr));
        ui->infoWidget->setStyleSheet(QString("QWidget {background-color:%1; color: #666666;}").arg(colorStr));
    } else {
        bgColorExp = QRegExp(R"(background.*:[ ]*rgb\((\d*),[ ]*(\d*),[ ]*(\d*)\))");
        if (bgColorExp.indexIn(html.trimmed()) != -1) {
            int r = bgColorExp.cap(1).toInt();
            int g = bgColorExp.cap(2).toInt();
            int b = bgColorExp.cap(3).toInt();
            QColor colorStr(r, g, b);
            ui->bodyWidget->setStyleSheet(QString("#bodyWidget {background-color:%1;}").arg(colorStr.name()));
            ui->infoWidget->setStyleSheet(QString("QWidget {background-color:%1; color: #666666;}").arg(colorStr.name()));
        }
    }
    ui->countLabel->setText(QString("%1 Characters").arg(html.size()));
}

void ClipboardItemInnerWidget::showImage(const QPixmap &pixmap) {
    this->imageLabel->show();
    this->imageLabel->setPixmap(pixmap.scaled(275, 234, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->countLabel->setText(QString("%1 x %2 Pixels").arg(pixmap.height()).arg(pixmap.width()));
    ui->typeLabel->setText(tr("Image"));
}

void ClipboardItemInnerWidget::showText(const QString &text) {
    if (QColor::isValidColor(text.trimmed())) {
        this->showColor(QColor(text.trimmed()));
    } else {
        this->textBrowser->show();
        this->textBrowser->setPlainText(text);
        ui->countLabel->setText(QString("%1 Characters").arg(text.size()));
    }
}

void ClipboardItemInnerWidget::showColor(const QColor &color) {
    this->imageLabel->show();
    QColor fontColor(255 - color.red(), 255 - color.green(), 255 - color.blue());

    this->imageLabel->setStyleSheet(QString("QLabel {background-color: %1; color: %2;}").arg(color.name(), fontColor.name()));
    ui->infoWidget->setStyleSheet(QString("QWidget {background-color: %1;}").arg(color.name()));
    ui->countLabel->setText("");
    this->imageLabel->setText(color.name().toUpper());
    ui->typeLabel->setText(tr("Color"));
}

void ClipboardItemInnerWidget::showUrls(const QList<QUrl> &urls) {
    this->textBrowser->show();
    QString str;
    foreach (const QUrl &url, urls) {
        str += url.toString() + "\n";
    }
    this->textBrowser->setText(str);
    ui->typeLabel->setText(tr("Links"));
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
