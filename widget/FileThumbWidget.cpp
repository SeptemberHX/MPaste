// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 FileThumbWidget 的实现逻辑，对图片文件走缩放预览，其余文件走图标预览。
// pos: widget 层中的 FileThumbWidget 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#include "FileThumbWidget.h"
#include "ui_FileThumbWidget.h"
#include "utils/MPasteSettings.h"
#include <QFileInfo>
#include <QFileIconProvider>
#include <QTextLayout>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QImageReader>
namespace {
QPixmap scaleToFillHeightCropWidth(const QPixmap &pixmap, const QSize &targetSize, qreal devicePixelRatio) {
    if (pixmap.isNull() || !targetSize.isValid()) {
        return pixmap;
    }

    const QSize pixelTargetSize = targetSize * devicePixelRatio;
    if (!pixelTargetSize.isValid()) {
        return pixmap;
    }

    QPixmap scaled = pixmap.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.width() > pixelTargetSize.width() || scaled.height() > pixelTargetSize.height()) {
        const int x = qMax(0, (scaled.width() - pixelTargetSize.width()) / 2);
        const int y = qMax(0, (scaled.height() - pixelTargetSize.height()) / 2);
        scaled = scaled.copy(x, y,
                             qMin(scaled.width(), pixelTargetSize.width()),
                             qMin(scaled.height(), pixelTargetSize.height()));
    }

    scaled.setDevicePixelRatio(devicePixelRatio);
    return scaled;
}

QPixmap loadScaledImageFilePreview(const QString &filePath, const QSize &targetSize, qreal devicePixelRatio) {
    const QSize pixelTargetSize = targetSize * devicePixelRatio;
    if (!pixelTargetSize.isValid()) {
        return QPixmap();
    }

    QImageReader reader(filePath);
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid()) {
        return QPixmap();
    }

    const QSize scaledReadSize = sourceSize.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding);
    if (scaledReadSize.isValid()) {
        reader.setScaledSize(scaledReadSize);
    }

    const QImage image = reader.read();
    if (image.isNull()) {
        return QPixmap();
    }

    return QPixmap::fromImage(image);
}

QString formatTwoLineEndElidedText(const QString &text, const QFont &font, const QFontMetrics &fontMetrics, int lineWidth) {
    if (text.isEmpty() || lineWidth <= 0) {
        return text;
    }

    QTextLayout layout(text, font);
    layout.beginLayout();

    QStringList lines;
    QString consumed;
    int textPosition = 0;
    for (int lineIndex = 0; lineIndex < 2; ++lineIndex) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(lineWidth);
        const int start = line.textStart();
        const int length = line.textLength();
        QString lineText = text.mid(start, length).trimmed();
        if (lineText.isEmpty()) {
            continue;
        }
        lines << lineText;
        textPosition = start + length;
        consumed = text.left(textPosition);
    }
    layout.endLayout();

    if (lines.isEmpty()) {
        return fontMetrics.elidedText(text, Qt::ElideRight, lineWidth);
    }

    if (textPosition >= text.size()) {
        return lines.join(QLatin1Char('\n'));
    }

    if (lines.size() == 1) {
        return fontMetrics.elidedText(text, Qt::ElideRight, lineWidth);
    }

    const QString remaining = text.mid(consumed.size()).trimmed();
    lines[1] = fontMetrics.elidedText(lines[1] + remaining, Qt::ElideRight, lineWidth);
    return lines.join(QLatin1Char('\n'));
}
}

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
    ui->pathLabel->setWordWrap(false);
    ui->pathLabel->setTextFormat(Qt::PlainText);
    ui->pathLabel->setContentsMargins(6, 0, 6, 0);

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

void FileThumbWidget::setPathVisible(bool visible) {
    ui->pathLabel->setVisible(visible);
}

void FileThumbWidget::showUrl(const QUrl &fileUrl) {
    // 图片文件显示缩放预览，其余文件显示系统图标。
    const qreal dpr = ui->iconLabel->devicePixelRatioF();
    const QSize targetSize = ui->iconLabel->size() * dpr;

    QPixmap pixmap;
    bool imageFile = false;

    QFileInfo info(fileUrl.toLocalFile());
    if (info.exists()) {
        const QMimeType mimeType = mimeDatabase_.mimeTypeForFile(info);
        imageFile = mimeType.name().startsWith(QStringLiteral("image/"));
        if (imageFile) {
            pixmap = loadScaledImageFilePreview(info.absoluteFilePath(), ui->iconLabel->size(), dpr);
        }
        if (pixmap.isNull()) {
            QFileIconProvider provider;
            QIcon icon = provider.icon(info);
            pixmap = icon.pixmap(targetSize);
            imageFile = false;
        }
    } else {
        pixmap = QPixmap(":/resources/resources/broken.svg");
    }

    QPixmap scaledPixmap = imageFile
        ? scaleToFillHeightCropWidth(pixmap, ui->iconLabel->size(), dpr)
        : pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!imageFile) {
        scaledPixmap.setDevicePixelRatio(dpr);
    }
    ui->iconLabel->setPixmap(scaledPixmap);

    setPathVisible(true);
    ui->pathLabel->setToolTip(fileUrl.toLocalFile());
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
    setPathVisible(true);
    ui->pathLabel->setToolTip(str);
    this->setElidedText(str);
}

void FileThumbWidget::setElidedText(const QString &str) {
    const QFont font = ui->pathLabel->font();
    const QFontMetrics fontMetrics(ui->pathLabel->font());
    const int horizontalPadding = 12;
    const int lineWidth = qMax(1, (ui->pathLabel->width() > 0
        ? ui->pathLabel->width()
        : ui->pathLabel->sizeHint().width()) - horizontalPadding);
    ui->pathLabel->setText(formatTwoLineEndElidedText(str, font, fontMetrics, lineWidth));
}
