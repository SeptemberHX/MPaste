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
            QPixmap picFile(info.absoluteFilePath());
            picFile.setDevicePixelRatio(this->devicePixelRatioF());
            ui->iconLabel->setPixmap(picFile.scaled(ui->iconLabel->size() * this->devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            QFileIconProvider provider;
            QIcon icon = provider.icon(info);
            QPixmap pixmap = icon.pixmap(ui->iconLabel->size() * this->devicePixelRatioF());
            pixmap.setDevicePixelRatio(this->devicePixelRatioF());
            ui->iconLabel->setPixmap(pixmap.scaled(ui->iconLabel->size() * this->devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    } else {
        QPixmap pixmap(":/resources/resources/broken.svg");
        pixmap.setDevicePixelRatio(this->devicePixelRatioF());
        ui->iconLabel->setPixmap(pixmap.scaled(ui->iconLabel->size() * this->devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    this->setElidedText(fileUrl.toLocalFile());
}

void FileThumbWidget::showUrls(const QList<QUrl> &fileUrls) {
    QPixmap pixmap(":/resources/resources/files.svg");
    pixmap.setDevicePixelRatio(this->devicePixelRatioF());
    ui->iconLabel->setPixmap(pixmap.scaled(ui->iconLabel->size() * this->devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    QString str;
    foreach (const QUrl &url, fileUrls) {
        str += url.fileName() + ", ";
    }
    str = str.remove(str.size() - 2, 2);
    this->setElidedText(str);
}

void FileThumbWidget::setElidedText(const QString &str) {
    QString r = str;
    QFontMetrics font = QFontMetrics(ui->pathLabel->font());
    if(font.boundingRect(str).width() > ui->pathLabel->width() * 2){
        r = font.elidedText(str, Qt::ElideRight, ui->pathLabel->width() * 2);
    }
    ui->pathLabel->setText(r);
}
