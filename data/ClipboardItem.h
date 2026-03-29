// input: Depends on Qt mime/image/text primitives and raw clipboard payloads from the system.
// output: Exposes a comparable clipboard item with cached search text, alias + pin metadata, and lightweight content fingerprint + image snapshot hints.
// pos: Data-layer core clipboard model used by persistence, filtering, dedup, and rendering code.
// update: If I change, update this header block and my folder README.md (data-layer preview kind for card rendering).
#ifndef MPASTE_CLIPBOARDITEM_H
#define MPASTE_CLIPBOARDITEM_H

#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QMimeData>
#include <QPixmap>
#include <QScopedPointer>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QUrl>
#include <QVariant>

#include <functional>
#include <optional>

#include "ContentType.h"
#include "ContentClassifier.h"
#include "PreviewKind.h"
#include "PreviewClassifier.h"

class ClipboardItem {
private:
    QString name_;
    QPixmap icon_;
    QDateTime time_;
    QScopedPointer<QMimeData> mimeData_;
    QPixmap favicon_;
    QString title_;
    QString url_;
    QString alias_;
    bool pinned_ = false;
    mutable std::optional<QByteArray> fingerprintCache_;
    mutable std::optional<QString> searchableTextCache_;
    mutable std::optional<QList<QUrl>> normalizedUrlsCache_;
    mutable std::optional<QPixmap> imageCache_;
    mutable std::optional<QSize> imageSizeCache_;

    // Lazy-load support (V4 format)
    QPixmap thumbnail_;
    bool thumbnailAvailableHint_ = false;
    QString sourceFilePath_;
    quint64 mimeDataFileOffset_ = 0;
    bool mimeDataLoaded_ = true;
    std::function<void(ClipboardItem&)> mimeDataLoader_;
    mutable ContentType cachedContentType_ = ::Text;
    mutable ClipboardPreviewKind cachedPreviewKind_ = ::TextPreview;
    mutable QString cachedNormalizedText_;
    mutable QList<QUrl> cachedNormalizedUrls_;

    // Implemented in ClipboardItem.cpp
    QList<QUrl> buildNormalizedUrls() const;
    QString buildNormalizedText() const;
    QString buildSearchableText() const;
    QByteArray buildFingerprint() const;
    static bool shouldCopyLightMimeFormat(const QString &format);

    bool hasDecodableImage() const {
        return ContentClassifier::hasDecodableImage(mimeData_.data());
    }

    bool hasFastImagePayload() const {
        return ContentClassifier::hasFastImagePayload(mimeData_.data());
    }

    QByteArray extractFastImagePayloadBytes(QString *formatOut = nullptr) const;

    bool shouldSkipImageDecodeFormat(const QString &format) const {
        return ContentClassifier::shouldSkipImageDecodeFormatName(format);
    }

    bool shouldTreatHtmlPayloadAsImage() const {
        return ContentClassifier::shouldTreatHtmlPayloadAsImage(mimeData_.data(), buildNormalizedText());
    }

    bool shouldTreatHtmlPayloadAsImageFast() const {
        return ContentClassifier::shouldTreatHtmlPayloadAsImageFast(mimeData_.data(), buildNormalizedText());
    }

    ContentType detectLightContentType() const {
        return ContentClassifier::classifyLight(mimeData_.data(), buildNormalizedText(), buildNormalizedUrls());
    }

    // Implemented in ClipboardItem.cpp
    QString htmlImageIdentity() const;

    void invalidateSearchCache() {
        searchableTextCache_.reset();
        normalizedUrlsCache_.reset();
    }

public:
    static bool shouldCopyExtraMimeFormat(const QString &format);
    static QStringView htmlFragment(const QString &html);

    ClipboardItem(const QPixmap &icon, const QMimeData *mimeData);
    ClipboardItem() = default;

    static ClipboardItem createLightweight(const QPixmap &icon, const QMimeData *mimeData);

    ClipboardItem(const ClipboardItem &other);
    ClipboardItem& operator=(const ClipboardItem &other);

    bool contains(const QString &keyword) const;
    bool containsDeep(const QString &keyword) const;

    bool operator==(const ClipboardItem &other) const;

    const QByteArray &fingerprint() const {
        if (!fingerprintCache_) {
            fingerprintCache_ = buildFingerprint();
        }
        return *fingerprintCache_;
    }

    void setFingerprintCache(const QByteArray &fp) {
        fingerprintCache_ = fp;
    }

    const QPixmap& getIcon() const { return icon_; }
    void setIcon(const QPixmap &icon) { icon_ = icon; }

    QMimeData* createMimeData() const {
        const_cast<ClipboardItem*>(this)->ensureMimeDataLoaded();
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

    const QMimeData* getMimeData() const {
        const_cast<ClipboardItem*>(this)->ensureMimeDataLoaded();
        return mimeData_.data();
    }
    QString getText() const { return mimeData_ ? mimeData_->text() : QString(); }
    QString getNormalizedText() const {
        if (!mimeDataLoaded_) {
            return cachedNormalizedText_;
        }
        return buildNormalizedText();
    }
    QList<QUrl> getNormalizedUrls() const {
        if (!mimeDataLoaded_) {
            return cachedNormalizedUrls_;
        }
        if (!normalizedUrlsCache_) {
            normalizedUrlsCache_ = buildNormalizedUrls();
        }
        return *normalizedUrlsCache_;
    }

    QPixmap getImage() const;

    QSize getImagePixelSize() const;
    QByteArray imagePayloadBytesFast() const { return extractFastImagePayloadBytes(); }

    QString getHtml() const { return mimeData_ ? mimeData_->html() : QString(); }
    ClipboardPreviewKind getPreviewKind() const {
        if (!mimeDataLoaded_) {
            return cachedPreviewKind_;
        }
        return PreviewClassifier::classifyFull(getContentType(),
                                               getNormalizedText(),
                                               mimeData_.data(),
                                               hasFastImagePayload(),
                                               hasDecodableImage());
    }
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
    QString getAlias() const { return alias_; }
    bool isPinned() const { return pinned_; }

    void setTitle(const QString &title) { title_ = title; invalidateSearchCache(); }
    void setUrl(const QString &url) { url_ = url; invalidateSearchCache(); }
    void setAlias(const QString &alias) { alias_ = alias; invalidateSearchCache(); }
    void setPinned(bool pinned) { pinned_ = pinned; }

    void setName(const QString &name) { name_ = name; }
    QString getName() const { return name_; }

    void setMimeFormat(const QString &format, const QByteArray &data) {
        if (!mimeData_) {
            mimeData_.reset(new QMimeData);
        }
        mimeData_->setData(format, data);
    }

    bool hasThumbnail() const { return !thumbnail_.isNull(); }
    bool hasThumbnailHint() const { return thumbnailAvailableHint_ || !thumbnail_.isNull(); }
    const QPixmap& thumbnail() const { return thumbnail_; }
    void setThumbnail(const QPixmap &thumb) {
        thumbnail_ = thumb;
        if (!thumb.isNull()) {
            thumbnailAvailableHint_ = true;
        }
    }
    void clearThumbnail() { thumbnail_ = QPixmap(); }
    void setThumbnailAvailableHint(bool available) {
        thumbnailAvailableHint_ = available || !thumbnail_.isNull();
    }
    void setSourceFilePath(const QString &path) { sourceFilePath_ = path; }
    const QString& sourceFilePath() const { return sourceFilePath_; }
    void setMimeDataFileOffset(quint64 offset) { mimeDataFileOffset_ = offset; }
    quint64 mimeDataFileOffset() const { return mimeDataFileOffset_; }
    bool isMimeDataLoaded() const { return mimeDataLoaded_; }

    void setLightLoaded(ContentType type,
                        const QString &normText,
                        const QList<QUrl> &normUrls,
                        bool hasThumbnailHint = false) {
        mimeDataLoaded_ = false;
        cachedContentType_ = type;
        cachedNormalizedText_ = normText;
        cachedNormalizedUrls_ = normUrls;
        thumbnailAvailableHint_ = hasThumbnailHint || !thumbnail_.isNull();
        cachedPreviewKind_ = PreviewClassifier::classifyLight(cachedContentType_,
                                                              cachedNormalizedText_,
                                                              thumbnailAvailableHint_,
                                                              mimeData_.data(),
                                                              hasFastImagePayload());
        if (cachedContentType_ == RichText && cachedPreviewKind_ == TextPreview) {
            thumbnail_ = QPixmap();
            thumbnailAvailableHint_ = false;
        }
    }

    void setMimeDataLoader(std::function<void(ClipboardItem&)> loader) {
        mimeDataLoader_ = std::move(loader);
    }

    void ensureMimeDataLoaded() {
        if (mimeDataLoaded_) return;
        mimeDataLoaded_ = true;
        if (mimeDataLoader_) {
            mimeDataLoader_(*this);
            // Invalidate caches that were built from light-loaded metadata.
            imageCache_.reset();
            imageSizeCache_.reset();
            searchableTextCache_.reset();
            normalizedUrlsCache_.reset();
        }
    }

    ContentType getContentType() const {
        if (!mimeDataLoaded_) {
            return cachedContentType_;
        }
        if (!mimeData_) return Text;

        return ContentClassifier::classifyFull(mimeData_.data(), getNormalizedText(), getNormalizedUrls());
    }
};

Q_DECLARE_METATYPE(ClipboardItem)

#endif // MPASTE_CLIPBOARDITEM_H
