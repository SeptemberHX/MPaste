#include "FileThumbWidget.h"
#include "ui_FileThumbWidget.h"
#include <QFileInfo>
#include <QFileIconProvider>
#include <QGraphicsDropShadowEffect>

FileThumbWidget::FileThumbWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FileThumbWidget)
{
    ui->setupUi(this);
    auto *coverShadow = new QGraphicsDropShadowEffect;
    coverShadow->setColor(Qt::gray);
    coverShadow->setBlurRadius(5);
    coverShadow->setOffset(3);
    ui->iconLabel->setGraphicsEffect(coverShadow);
    ui->iconLabel->setAttribute(Qt::WA_TranslucentBackground);
}

FileThumbWidget::~FileThumbWidget()
{
    delete ui;
}

void FileThumbWidget::showUrl(const QUrl &fileUrl) {
    QFileInfo info(fileUrl.toLocalFile());
    if (info.exists()) {
        QMimeType mime = db.mimeTypeForFile(info);
        if (mime.name().startsWith("image")) {
            ui->iconLabel->setPixmap(QPixmap(info.absoluteFilePath()).scaled(ui->iconLabel->width(), ui->iconLabel->width(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            QFileIconProvider provider;
            QIcon icon = provider.icon(info);
            ui->iconLabel->setPixmap(icon.pixmap(ui->iconLabel->width(), ui->iconLabel->width()));
        }
    } else {
        ui->iconLabel->setPixmap(QPixmap(":/resources/resources/broken.svg").scaled(ui->iconLabel->width(), ui->iconLabel->width(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    ui->pathLabel->setText(fileUrl.toLocalFile());
}
