#include "FileThumbWidget.h"
#include "ui_FileThumbWidget.h"
#include <QFileInfo>
#include <QFileIconProvider>

FileThumbWidget::FileThumbWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FileThumbWidget)
{
    ui->setupUi(this);
}

FileThumbWidget::~FileThumbWidget()
{
    delete ui;
}

void FileThumbWidget::showUrl(const QUrl &fileUrl) {
    QFileInfo info(fileUrl.toLocalFile());
    if (info.exists()) {
        QFileIconProvider provider;
        QIcon icon = provider.icon(info);
        ui->iconLabel->setPixmap(icon.pixmap(100, 100));
    } else {
        ui->iconLabel->setPixmap(QPixmap(":/resources/resources/broken.svg").scaled(100, 100));
    }
    ui->pathLabel->setText(fileUrl.toLocalFile());
}
