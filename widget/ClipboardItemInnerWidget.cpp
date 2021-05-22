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
    this->plainTextEdit = new QPlainTextEdit(ui->bodyWidget);
    this->mLayout->addWidget(this->plainTextEdit);
    this->plainTextEdit->hide();

    this->textEdit = new QTextEdit(ui->bodyWidget);
    this->textEdit->setWordWrapMode(QTextOption::WrapAnywhere);
    this->textEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    this->mLayout->addWidget(this->textEdit);
    this->textEdit->hide();

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
        this->textEdit->show();
        this->textEdit->setHtml(item.getHtml());
        this->textEdit->setAcceptRichText(true);
    } else if (!item.getText().isEmpty()) {
        this->textEdit->show();
        this->textEdit->setPlainText(item.getText());
    }
}
