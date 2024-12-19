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
    // 获取一次 DPI 比例和目标尺寸
    const qreal dpr = ui->iconLabel->devicePixelRatioF();
    const QSize targetSize = ui->iconLabel->size() * dpr;

    QPixmap pixmap;

    QFileInfo info(fileUrl.toLocalFile());
    if (info.exists()) {
        QMimeType mime = db.mimeTypeForFile(info);
        if (mime.name().startsWith("image")) {
            pixmap = QPixmap(info.absoluteFilePath());
        } else {
            QFileIconProvider provider;
            QIcon icon = provider.icon(info);
            pixmap = icon.pixmap(targetSize);
        }
    } else {
        pixmap = QPixmap(":/resources/resources/broken.svg");
    }

    // 统一处理 DPI 缩放
    QPixmap scaledPixmap = pixmap.scaled(targetSize,
                                        Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
    scaledPixmap.setDevicePixelRatio(dpr);
    ui->iconLabel->setPixmap(scaledPixmap);

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
