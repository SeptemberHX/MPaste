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
    this->topBgColor = QColor(r/n, g/n, b/n, 0);
    this->refreshStyleSheet();
}

void ClipboardItemInnerWidget::showItem(ClipboardItem item) {
    this->setIcon(item.getIcon());

    if (!item.getHtml().isEmpty()) {
        QRegExp regExp("<([A-Za-z]+)");
        regExp.indexIn(item.getHtml());
        QSet<QString> tagSet;
        int pos = 0;
        while ((pos = regExp.indexIn(item.getHtml(), pos)) != -1) {
            QString str = regExp.cap(1);
            tagSet << str;
            pos += regExp.matchedLength();
        }

        if (tagSet.contains("img") && tagSet.contains("meta") && tagSet.size() == 2) {
            this->imageLabel->show();
            this->imageLabel->setPixmap(item.getImage().scaled(275, 234, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            ui->countLabel->setText(QString("%1 x %2 Pixels").arg(item.getImage().height()).arg(item.getImage().width()));
            ui->typeLabel->setText(tr("Image"));
        } else {
            this->textBrowser->show();
            this->textBrowser->setHtml(item.getHtml());

            QRegExp bgColorExp("background-color:(#[A-Za-z0-9]{6})");
            if (bgColorExp.indexIn(item.getHtml()) != -1) {
                QString colorStr = bgColorExp.cap(1);
                ui->bodyWidget->setStyleSheet(QString("#bodyWidget {background-color:%1;}").arg(colorStr));
                ui->infoWidget->setStyleSheet(QString("QWidget {background-color:%1; color: #666666;}").arg(colorStr));
            }
            ui->countLabel->setText(QString("%1 Characters").arg(item.getText().size()));
        }
    } else if (!item.getText().isEmpty()) {
        this->textBrowser->show();
        this->textBrowser->setPlainText(item.getText());
        ui->countLabel->setText(QString("%1 Characters").arg(item.getText().size()));
    } else if (!item.getImage().isNull()) {
        this->imageLabel->show();
        this->imageLabel->setPixmap(item.getImage().scaled(275, 234, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        ui->countLabel->setText(QString("%1 x %2 Pixels").arg(item.getImage().height()).arg(item.getImage().width()));
        ui->typeLabel->setText(tr("Image"));
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
