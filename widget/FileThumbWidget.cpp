// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 FileThumbWidget 的实现逻辑，统一使用文件图标预览文件条目。
// pos: widget 层中的 FileThumbWidget 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#include "FileThumbWidget.h"
#include "ui_FileThumbWidget.h"
#include "utils/MPasteSettings.h"
#include <QFileInfo>
#include <QFileIconProvider>
#include <QGraphicsDropShadowEffect>

FileThumbWidget::FileThumbWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FileThumbWidget)
{
    ui->setupUi(this);

    int scale = MPasteSettings::getInst()->getItemScale();

    // Scale icon and path label heights
    int iconH = 150 * scale / 100;
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->setSpacing(0);
    ui->verticalLayout->setStretch(0, 1);
    ui->iconLabel->setMinimumSize(180 * scale / 100, iconH);
    ui->iconLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    int pathH = 50 * scale / 100;
    ui->pathLabel->setMinimumHeight(pathH);
    ui->pathLabel->setMaximumHeight(pathH);
    ui->pathLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // Scale font
    QFont f = ui->pathLabel->font();
    f.setPointSize(9 * scale / 100);
    ui->pathLabel->setFont(f);
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
    // 统一按文件图标渲染，避免本地图片文件再次走整图解码。
    const qreal dpr = ui->iconLabel->devicePixelRatioF();
    const QSize targetSize = ui->iconLabel->size() * dpr;

    QPixmap pixmap;

    QFileInfo info(fileUrl.toLocalFile());
    if (info.exists()) {
        QFileIconProvider provider;
        QIcon icon = provider.icon(info);
        pixmap = icon.pixmap(targetSize);
    } else {
        pixmap = QPixmap(":/resources/resources/broken.svg");
    }

    QPixmap scaledPixmap = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
