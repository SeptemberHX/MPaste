// input: Depends on Qt mime/image/text primitives and raw clipboard payloads from the system.
// output: Exposes a comparable clipboard item with cached search text and lightweight content fingerprint.
// pos: Data-layer core clipboard model used by persistence, filtering, dedup, and rendering code.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARDITEM_H
#define MPASTE_CLIPBOARDITEM_H

#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QImage>
#include <QMimeData>
#include <QPixmap>
#include <QScopedPointer>
#include <QStringList>
#include <QTextDocument>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <cstring>

class ClipboardItem {
public:
    enum ContentType { All = 0, Text, Link, Image, RichText, File, Color };

private:
    QString name_;
    QPixmap icon_;
    QDateTime time_;
    QScopedPointer<QMimeData> mimeData_;
    QPixmap favicon_;
    QString title_;
    QString url_;
    mutable QByteArray fingerprintCache_;
    mutable bool fingerprintCacheInitialized_ = false;
    mutable QString searchableTextCache_;
    mutable bool searchableTextCacheInitialized_ = false;

    QString buildSearchableText() const {
        if (!mimeData_) {
            return {};
        }

        QStringList parts;
        if (!title_.isEmpty()) {
            parts << title_;
        }
        if (!url_.isEmpty()) {
            parts << url_;
        }
        if (mimeData_->hasText()) {
            parts << mimeData_->text();
        }
        if (mimeData_->hasHtml()) {
            QTextDocument doc;
            doc.setHtml(mimeData_->html());
            parts << doc.toPlainText();
        }
        if (mimeData_->hasUrls()) {
            for (const QUrl &url : mimeData_->urls()) {
                parts << url.toString();
            }
        }

        for (const QString &format : mimeData_->formats()) {
            if (format.startsWith("text/")
                || format.contains("plain")
                || format.contains("html")
                || format.contains("xml")
                || format.contains("json")) {
                parts << QString::fromUtf8(mimeData_->data(format));
            }
        }

        parts.removeAll(QString());
        return parts.join(QLatin1Char('\n')).toLower();
    }

    QByteArray buildFingerprint() const {
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(QByteArray::number(static_cast<int>(getContentType())));

        if (!mimeData_) {
            return hash.result();
        }

        if (mimeData_->hasText()) {
            hash.addData(mimeData_->text().simplified().toUtf8());
        } else if (mimeData_->hasHtml()) {
            QTextDocument doc;
            doc.setHtml(mimeData_->html());
            hash.addData(doc.toPlainText().simplified().toUtf8());
        }

        if (mimeData_->hasUrls()) {
            for (const QUrl &url : mimeData_->urls()) {
                hash.addData(url.toString(QUrl::FullyEncoded).toUtf8());
                hash.addData("\n", 1);
            }
        }

        if (mimeData_->hasColor()) {
            hash.addData(QByteArray::number(static_cast<quint32>(getColor().rgba())));
        }

        if (mimeData_->hasImage()) {
            QByteArray imageBytes;
            const QStringList preferredFormats = {
                QStringLiteral("application/x-qt-image"),
                QStringLiteral("image/png"),
                QStringLiteral("image/jpeg"),
                QStringLiteral("image/bmp")
            };

            for (const QString &format : preferredFormats) {
                imageBytes = mimeData_->data(format);
                if (!imageBytes.isEmpty()) {
                    break;
                }
            }

            if (imageBytes.isEmpty()) {
                QPixmap pixmap = getImage();
                if (!pixmap.isNull()) {
                    QBuffer buffer(&imageBytes);
                    buffer.open(QIODevice::WriteOnly);
                    pixmap.save(&buffer, "PNG");
                }
            }

            if (!imageBytes.isEmpty()) {
                hash.addData(imageBytes);
            }
        }

        if (!mimeData_->hasText() && !mimeData_->hasHtml() && !mimeData_->hasUrls()
            && !mimeData_->hasImage() && !mimeData_->hasColor()) {
            for (const QString &format : mimeData_->formats()) {
                hash.addData(format.toUtf8());
                hash.addData(mimeData_->data(format));
            }
        }

        return hash.result();
    }

    void invalidateSearchCache() {
        searchableTextCacheInitialized_ = false;
        searchableTextCache_.clear();
    }

public:
    ClipboardItem(const QPixmap &icon, const QMimeData *mimeData) : icon_(icon) {
        if (mimeData) {
            mimeData_.reset(new QMimeData);

            if (mimeData->hasImage()) {
                QImage image = qvariant_cast<QImage>(mimeData->imageData());
                QByteArray imageData;
                QBuffer buffer(&imageData);
                buffer.open(QIODevice::WriteOnly);
                image.save(&buffer, "PNG");
                mimeData_->setData("application/x-qt-image", imageData);
                mimeData_->setData("application/x-qt-windows-mime;value=\"PNG\"", imageData);
            }

            for (const QString &format : mimeData->formats()) {
                mimeData_->setData(format, mimeData->data(format));
            }

            time_ = QDateTime::currentDateTime();
            name_ = QString::number(time_.toMSecsSinceEpoch());
        }
    }

    ClipboardItem() = default;

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
        fingerprintCache_ = other.fingerprintCache_;
        fingerprintCacheInitialized_ = other.fingerprintCacheInitialized_;
        searchableTextCache_ = other.searchableTextCache_;
        searchableTextCacheInitialized_ = other.searchableTextCacheInitialized_;
    }

    ClipboardItem& operator=(const ClipboardItem &other) {
        if (this != &other) {
            time_ = other.time_;
            name_ = other.name_;
            favicon_ = other.favicon_;
            title_ = other.title_;
            url_ = other.url_;
            icon_ = other.icon_;
            fingerprintCache_ = other.fingerprintCache_;
            fingerprintCacheInitialized_ = other.fingerprintCacheInitialized_;
            searchableTextCache_ = other.searchableTextCache_;
            searchableTextCacheInitialized_ = other.searchableTextCacheInitialized_;

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
        if (!mimeData_ || keyword.isEmpty()) {
            return false;
        }

        if (!searchableTextCacheInitialized_) {
            searchableTextCache_ = buildSearchableText();
            searchableTextCacheInitialized_ = true;
        }

        return searchableTextCache_.contains(keyword.toLower());
    }

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
        if (!mimeData_ && !other.mimeData_) {
            return true;
        }
        if (!mimeData_ || !other.mimeData_) {
            return false;
        }

        bool textMatched = false;
        if (mimeData_->hasText() || other.mimeData_->hasText()) {
            if (mimeData_->text() != other.mimeData_->text()) {
                return false;
            }
            textMatched = true;
        }

        if (mimeData_->hasHtml() || other.mimeData_->hasHtml()) {
            QStringView frag1 = htmlFragment(mimeData_->html());
            QStringView frag2 = htmlFragment(other.mimeData_->html());
            if (frag1 != frag2) {
                if (!textMatched || (!frag1.startsWith(frag2) && !frag2.startsWith(frag1))) {
                    return false;
                }
            }
        }

        if (mimeData_->hasUrls() || other.mimeData_->hasUrls()) {
            if (mimeData_->urls() != other.mimeData_->urls()) {
                return false;
            }
        }

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
                for (int y = 0; y < i1.height(); ++y) {
                    if (std::memcmp(i1.constScanLine(y), i2.constScanLine(y), i1.bytesPerLine()) != 0) {
                        return false;
                    }
                }
            }
        }

        if (mimeData_->hasColor() || other.mimeData_->hasColor()) {
            if (getColor() != other.getColor()) {
                return false;
            }
        }

        return true;
    }

    const QByteArray &fingerprint() const {
        if (!fingerprintCacheInitialized_) {
            fingerprintCache_ = buildFingerprint();
            fingerprintCacheInitialized_ = true;
        }
        return fingerprintCache_;
    }

    const QPixmap& getIcon() const { return icon_; }

    QMimeData* createMimeData() const {
        if (!mimeData_) {
            return nullptr;
        }

        QMimeData* newMimeData = new QMimeData;
        for (const QString &format : mimeData_->formats()) {
            QByteArray data = mimeData_->data(format);
            if (!data.isEmpty()) {
                newMimeData->setData(format, data);
            }
        }
        return newMimeData;
    }

    const QMimeData* getMimeData() const { return mimeData_.data(); }
    QString getText() const { return mimeData_ ? mimeData_->text() : QString(); }

    QPixmap getImage() const {
        if (!mimeData_ || !mimeData_->hasImage()) {
            return QPixmap();
        }

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

        static const QStringList commonFormats = {
            QStringLiteral("image/png"),
            QStringLiteral("image/jpeg"),
            QStringLiteral("image/gif"),
            QStringLiteral("image/bmp"),
            QStringLiteral("application/x-qt-image")
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

    void setTitle(const QString &title) { title_ = title; invalidateSearchCache(); }
    void setUrl(const QString &url) { url_ = url; invalidateSearchCache(); }

    void setName(const QString &name) { name_ = name; }
    QString getName() const { return name_; }

    ContentType getContentType() const {
        if (!mimeData_) return Text;

        if (mimeData_->hasColor()) return Color;

        if (mimeData_->hasUrls() && !mimeData_->hasHtml()) {
            QList<QUrl> urls = mimeData_->urls();
            bool allValid = !urls.isEmpty() && std::all_of(urls.begin(), urls.end(),
                [](const QUrl &url) { return url.isValid() && !url.isRelative(); });
            if (allValid) {
                bool allFiles = std::all_of(urls.begin(), urls.end(),
                    [](const QUrl &url) { return url.isLocalFile(); });
                return allFiles ? File : Link;
            }
            return Text;
        }

        if (mimeData_->hasHtml()) {
            QString text = mimeData_->text().trimmed();
            if (!mimeData_->formats().contains("Rich Text Format")
                && !text.contains("\n")
                && (text.startsWith("http://") || text.startsWith("https://"))) {
                return Link;
            }
            return RichText;
        }

        if (mimeData_->hasImage()) return Image;
        return Text;
    }
};

#endif // MPASTE_CLIPBOARDITEM_H
