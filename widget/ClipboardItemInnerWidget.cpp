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
//    this->textBrowser->setEnabled(false);
    this->mLayout->addWidget(this->textBrowser);
    this->textBrowser->hide();

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
    ui->topWidget->setStyleSheet(QString("QWidget {background-color: %1; color: #FFFFFF;}").arg(c.name()));
}

QString ClipboardItemInnerWidget::genStyleSheetStr(QColor bgColor, QColor topColor) {
    return QString("QWidget {background-color: %1;} #topWidget { background-color: %2; } ").arg(bgColor.name(), topColor.name());
}

void ClipboardItemInnerWidget::showItem(ClipboardItem item) {
    this->setIcon(item.getIcon());

    if (!item.getHtml().isEmpty()) {
        this->textBrowser->show();
        this->textBrowser->setHtml(item.getHtml());
    } else if (!item.getText().isEmpty()) {
        this->textBrowser->show();
        this->textBrowser->setPlainText(item.getText());
    }
}
