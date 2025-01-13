//
// Created by ragdoll on 2021/5/22.
//

#ifndef MPASTE_CLIPBOARDITEM_H
#define MPASTE_CLIPBOARDITEM_H

#include <QMimeData>
#include <QPixmap>
#include <QDateTime>
#include <QVariant>
#include <QUrl>
#include <QBuffer>

class ClipboardItem {
private:
    QString name_;
    QPixmap icon_;
    QDateTime time_;
    QScopedPointer<QMimeData> mimeData_;

    // og item
    QPixmap favicon_;
    QString title_;
    QString url_;

public:
    // 构造函数
    ClipboardItem(const QPixmap &icon, const QMimeData *mimeData) : icon_(icon) {
        if (mimeData) {
            mimeData_.reset(new QMimeData);

            // 检查图像数据，直接获取；例如 snipaste 不会写到 data() 里
            if (mimeData->hasImage()) {
                QImage image = qvariant_cast<QImage>(mimeData->imageData());
                // 将图像数据存储为 PNG 格式
                QByteArray imageData;
                QBuffer buffer(&imageData);
                buffer.open(QIODevice::WriteOnly);
                image.save(&buffer, "PNG");  // 或者其他支持的格式

                mimeData_->setData("application/x-qt-image", imageData); // 直接设置PNG图像数据
                mimeData_->setData("application/x-qt-windows-mime;value=\"PNG\"", imageData); // 直接设置PNG图像数据
            }

            for (const QString &format : mimeData->formats()) {
                // qDebug() << format;
                mimeData_->setData(format, mimeData->data(format));
            }

            time_ = QDateTime::currentDateTime();
            name_ = QString::number(time_.toMSecsSinceEpoch());
        }
    }

    // 默认构造函数
    ClipboardItem() = default;

    // 拷贝构造函数
    ClipboardItem(const ClipboardItem &other) : icon_(other.icon_) {
        if (other.mimeData_) {
            mimeData_.reset(new QMimeData);
            for (const QString &format : other.mimeData_->formats()) {
                mimeData_->setData(format, other.mimeData_->data(format));
            }
        }

        time_ = other.time_;
        name_ = other.name_;
        favicon_ = other.favicon_;
        title_ = other.title_;
        url_ = other.url_;
        icon_ = other.icon_;
    }

    // 拷贝赋值操作符
    ClipboardItem& operator=(const ClipboardItem &other) {
        if (this != &other) {
            time_ = other.time_;
            name_ = other.name_;
            favicon_ = other.favicon_;
            title_ = other.title_;
            url_ = other.url_;
            icon_ = other.icon_;

            if (other.mimeData_) {
                mimeData_.reset(new QMimeData);
                for (const QString &format : other.mimeData_->formats()) {
                    mimeData_->setData(format, other.mimeData_->data(format));
                }
            } else {
                mimeData_.reset();
            }
        }
        return *this;
    }

    bool contains(const QString &keyword) const {
        if (!mimeData_) {
            return false;
        }

        const QString keywordLower = keyword.toLower();

        // 检查文本内容
        if (mimeData_->hasText() && mimeData_->text().toLower().contains(keywordLower)) {
            return true;
        }

        // 检查HTML内容
        if (mimeData_->hasHtml() && mimeData_->html().toLower().contains(keywordLower)) {
            return true;
        }

        // 检查URLs
        if (mimeData_->hasUrls()) {
            for (const QUrl &url : mimeData_->urls()) {
                if (url.toString().toLower().contains(keywordLower)) {
                    return true;
                }
            }
        }

        // 检查所有可用格式中的文本数据
        for (const QString &format : mimeData_->formats()) {
            // 只检查可能包含文本的格式
            if (format.startsWith("text/") ||
                format.contains("plain") ||
                format.contains("html") ||
                format.contains("xml") ||
                format.contains("json")) {

                QString text = QString::fromUtf8(mimeData_->data(format));
                if (text.toLower().contains(keywordLower)) {
                    return true;
                }
                }
        }

        return false;
    }

    bool operator==(const ClipboardItem &other) const {
        // 如果两者都为空，认为相等
        if (!mimeData_ && !other.mimeData_) {
            return true;
        }
        // 如果其中一个为空，另一个不为空，认为不相等
        if (!mimeData_ || !other.mimeData_) {
            return false;
        }

        // 比较文本内容
        if (mimeData_->hasText() || other.mimeData_->hasText()) {
            if (mimeData_->text() != other.mimeData_->text()) {
                return false;
            }
        }

        // 比较HTML内容
        if (mimeData_->hasHtml() || other.mimeData_->hasHtml()) {
            if (mimeData_->html() != other.mimeData_->html()) {
                return false;
            }
        }

        // 比较URLs
        if (mimeData_->hasUrls() || other.mimeData_->hasUrls()) {
            if (mimeData_->urls() != other.mimeData_->urls()) {
                return false;
            }
        }

        // 比较图片内容
        if (mimeData_->hasImage() || other.mimeData_->hasImage()) {
            QPixmap img1 = getImage();
            QPixmap img2 = other.getImage();
            if (img1.isNull() != img2.isNull() ||
                (!img1.isNull() && img1.toImage() != img2.toImage())) {
                return false;
                }
        }

        // 比较颜色数据
        if (mimeData_->hasColor() || other.mimeData_->hasColor()) {
            if (getColor() != other.getColor()) {
                return false;
            }
        }

        return true;
    }

    // Getter 方法保持不变
    const QPixmap& getIcon() const { return icon_; }

    // 修改 getMimeData 返回新的副本
    QMimeData* createMimeData() const {
        if (!mimeData_) {
            return nullptr;
        }

        // qDebug() << "Original formats:" << mimeData_->formats();

        QMimeData* newMimeData = new QMimeData;

        // 只复制非空数据
        for (const QString &format : mimeData_->formats()) {
            QByteArray data = mimeData_->data(format);
            if (!data.isEmpty()) {
                // qDebug() << "Copying format:" << format << "size:" << data.size();
                newMimeData->setData(format, data);
            } else {
                // qDebug() << "Skipping empty format:" << format;
            }
        }

        // qDebug() << "New formats:" << newMimeData->formats();

        return newMimeData;
    }

    // 保留只读访问
    const QMimeData* getMimeData() const { return mimeData_.data(); }
    // 常用访问器保持不变
    QString getText() const { return mimeData_ ? mimeData_->text() : QString(); }

    QPixmap getImage() const {
        if (!mimeData_ || !mimeData_->hasImage()) {
            return QPixmap();
        }

        // Windows 特定格式
        const QStringList formats = mimeData_->formats();
        for (const QString &format : formats) {
            if (format.startsWith("application/x-qt-windows-mime;value=\"")) {
                QPixmap pixmap;
                QByteArray data = mimeData_->data(format);
                if (!data.isEmpty() && pixmap.loadFromData(data)) {
                    return pixmap;
                }
            }
        }

        // 常见图片格式
        static const QStringList commonFormats = {
            "image/png",
            "image/jpeg",
            "image/gif",
            "image/bmp",
            "application/x-qt-image"
        };

        for (const QString &format : commonFormats) {
            if (mimeData_->hasFormat(format)) {
                QPixmap pixmap;
                QByteArray data = mimeData_->data(format);
                if (!data.isEmpty() && pixmap.loadFromData(data)) {
                    return pixmap;
                }
            }
        }

        // 首先尝试直接获取图片数据
        QVariant imageData = mimeData_->imageData();
        if (imageData.canConvert<QPixmap>()) {
            return qvariant_cast<QPixmap>(imageData);
        }
        if (imageData.canConvert<QImage>()) {
            QImage img = qvariant_cast<QImage>(imageData);
            if (!img.isNull()) {
                return QPixmap::fromImage(img);
            }
        }

        return QPixmap();
    }

    QString getHtml() const { return mimeData_ ? mimeData_->html() : QString(); }
    QList<QUrl> getUrls() const { return mimeData_ ? mimeData_->urls() : QList<QUrl>(); }
    QColor getColor() const {
        return mimeData_ && mimeData_->hasColor()
            ? qvariant_cast<QColor>(mimeData_->colorData())
            : QColor();
    }

    void setTime(const QDateTime &time) { time_ = time; }
    QDateTime getTime() const { return time_; }

    void setFavicon(const QPixmap &pixmap) { favicon_ = pixmap; }
    const QPixmap& getFavicon() const { return favicon_; }

    QString getTitle() const { return title_; }
    QString getUrl() const { return url_; }

    void setTitle(const QString &title) { title_ = title; }
    void setUrl(const QString &url) { url_ = url; }

    QString getName() const { return name_; }
};

#endif //MPASTE_CLIPBOARDITEM_H
