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
#include <algorithm>

class ClipboardItem {
public:
    enum ContentType { All = 0, Text, Link, Image, RichText, File, Color };

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

    // 提取HTML中Fragment标记之间的实际内容，忽略Word等应用每次复制都会变化的header元数据
    static QStringView htmlFragment(const QString &html) {
        static const QString startMarker = QStringLiteral("<!--StartFragment-->");
        static const QString endMarker = QStringLiteral("<!--EndFragment-->");
        int start = html.indexOf(startMarker);
        int end = html.indexOf(endMarker);
        if (start >= 0 && end > start) {
            return QStringView(html).mid(start + startMarker.length(), end - start - startMarker.length());
        }
        return QStringView(html);
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
        bool textMatched = false;
        if (mimeData_->hasText() || other.mimeData_->hasText()) {
            if (mimeData_->text() != other.mimeData_->text()) {
                return false;
            }
            textMatched = true;
        }

        // 比较HTML内容（提取Fragment部分，忽略Word等应用每次复制都会变化的header元数据）
        // 文本一致时，若HTML是截断关系（一个是另一个的前缀），视为同一内容
        if (mimeData_->hasHtml() || other.mimeData_->hasHtml()) {
            QStringView frag1 = htmlFragment(mimeData_->html());
            QStringView frag2 = htmlFragment(other.mimeData_->html());
            if (frag1 != frag2) {
                if (!textMatched || (!frag1.startsWith(frag2) && !frag2.startsWith(frag1))) {
                    return false;
                }
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
            if (img1.isNull() != img2.isNull()) {
                return false;
            }
            if (!img1.isNull()) {
                QImage i1 = img1.toImage();
                QImage i2 = img2.toImage();
                if (i1.size() != i2.size() || i1.format() != i2.format()) {
                    return false;
                }
                // 逐行比较原始字节，比逐像素比较快
                for (int y = 0; y < i1.height(); ++y) {
                    if (memcmp(i1.constScanLine(y), i2.constScanLine(y), i1.bytesPerLine()) != 0) {
                        return false;
                    }
                }
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

    void setName(const QString &name) { name_ = name; }
    QString getName() const { return name_; }

    ContentType getContentType() const {
        if (!mimeData_) return Text;

        // Color highest priority
        if (mimeData_->hasColor()) return Color;

        // URLs without HTML: File or Link
        if (mimeData_->hasUrls() && !mimeData_->hasHtml()) {
            QList<QUrl> urls = mimeData_->urls();
            bool allValid = !urls.isEmpty() && std::all_of(urls.begin(), urls.end(),
                [](const QUrl &url) { return url.isValid() && !url.isRelative(); });
            if (allValid) {
                // Check if all URLs are local files
                bool allFiles = std::all_of(urls.begin(), urls.end(),
                    [](const QUrl &url) { return url.isLocalFile(); });
                return allFiles ? File : Link;
            }
            return Text;
        }

        // HTML: Link or RichText
        if (mimeData_->hasHtml()) {
            QString text = mimeData_->text().trimmed();
            if (!mimeData_->formats().contains("Rich Text Format") &&
                !text.contains("\n") &&
                (text.startsWith("http://") || text.startsWith("https://"))) {
                return Link;
            }
            return RichText;
        }

        // Image
        if (mimeData_->hasImage()) return Image;

        // Fallback: Text
        return Text;
    }
};

#endif //MPASTE_CLIPBOARDITEM_H
