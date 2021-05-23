#include <iostream>
#include "ClipboardItemInnerWidget.h"
#include "ui_ClipboardItemInnerWidget.h"
#include <QPlainTextEdit>

ClipboardItemInnerWidget::ClipboardItemInnerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ClipboardItemInnerWidget),
    bgColor(Qt::white)
{
    ui->setupUi(this);
    this->mLayout = new QHBoxLayout(ui->bodyWidget);
    this->mLayout->setMargin(0);

    this->textBrowser = new MTextBrowser(ui->bodyWidget);
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
    this->mLayout->addWidget(this->imageLabel);

    ui->iconLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->widget_2->setAttribute(Qt::WA_TranslucentBackground);
    ui->typeLabel->setAttribute(Qt::WA_TranslucentBackground);
    ui->timeLabel->setAttribute(Qt::WA_TranslucentBackground);

    this->setStyleSheet(genStyleSheetStr(this->bgColor, this->bgColor));
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
    std::cout << r / n << " " << g / n << " " << b / n << " " << a / n << std::endl;
    QColor c(r/n, g/n, b/n, 0);
    ui->topWidget->setStyleSheet(this->genStyleSheetStr(this->bgColor, c));
}

QString ClipboardItemInnerWidget::genStyleSheetStr(QColor bgColor, QColor topColor) {
    return QString("QWidget {background-color: %1; color: #000000; } #topWidget { background-color: %2;} #typeLabel, #timeLabel { color: #FFFFFF; } ").arg(bgColor.name(), topColor.name());
}

void ClipboardItemInnerWidget::showItem(ClipboardItem item) {
    this->setIcon(item.getIcon());

    if (!item.getHtml().isEmpty()) {
        std::cout << item.getHtml().toStdString() << std::endl;
        QRegExp regExp("<([A-Za-z]+)");
        regExp.indexIn(item.getHtml());
        QSet<QString> tagSet;
        int pos = 0;
        while ((pos = regExp.indexIn(item.getHtml(), pos)) != -1) {
            QString str = regExp.cap(1);
//            std::cout << str.toStdString() << std::endl;
            tagSet << str;
            pos += regExp.matchedLength();
        }

        if (tagSet.contains("img") && tagSet.contains("meta") && tagSet.size() == 2) {
            this->imageLabel->show();
            std::cout << ui->bodyWidget->height() << std::endl;
            this->imageLabel->setPixmap(item.getImage().scaled(275, 234, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            ui->countLabel->setText(QString("%1 x %2 Pixels").arg(item.getImage().height()).arg(item.getImage().width()));
            ui->typeLabel->setText(tr("Image"));
        } else {
            this->textBrowser->show();
            this->textBrowser->setHtml(item.getHtml());

            QRegExp bgColorExp("background-color:(#[A-Za-z0-9]{6})");
            if (bgColorExp.indexIn(item.getHtml()) != -1) {
                QString colorStr = bgColorExp.cap(1);
                std::cout << colorStr.toStdString() << std::endl;
                ui->bodyWidget->setStyleSheet(QString("#bodyWidget {background-color:%1;}").arg(colorStr));
            }
            ui->countLabel->setText(QString("%1 Characters").arg(item.getText().size()));
        }
    } else if (!item.getText().isEmpty()) {
        this->textBrowser->show();
        this->textBrowser->setPlainText(item.getText());
    } else if (!item.getImage().isNull()) {
        this->imageLabel->show();
        this->imageLabel->setPixmap(item.getImage().scaled(this->imageLabel->sizeHint(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
}
