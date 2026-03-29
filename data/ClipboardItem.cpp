// input: Clipboard MIME payloads, cached metadata, and image/URL parsing utilities.
// output: ClipboardItem method implementations for fingerprinting, comparison, search, and content normalization.
// pos: Data-layer core clipboard model implementation extracted from ClipboardItem.h.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardItem.h"
#include "ClipboardItemUrlParser.h"
#include "ClipboardItemImageDecoder.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QRegularExpression>

#include <algorithm>
#include <cstring>

// --- Constructor ---

ClipboardItem::ClipboardItem(const QPixmap &icon, const QMimeData *mimeData) : icon_(icon) {
    if (mimeData) {
        mimeData_.reset(new QMimeData);

        for (const QString &format : mimeData->formats()) {
            mimeData_->setData(format, mimeData->data(format));
        }

        ClipboardImageDecoder::materializeCanonicalImage(
            mimeData_.data(),
            ClipboardImageDecoder::decodePixmapFromMimeData(mimeData));

        time_ = QDateTime::currentDateTime();
        name_ = QString::number(time_.toMSecsSinceEpoch());
    }
}

// --- URL building ---

QList<QUrl> ClipboardItem::buildNormalizedUrls() const {
    if (!mimeData_) {
        return {};
    }

    QList<QUrl> urls = mimeData_->urls();
    if (!urls.isEmpty()) {
        for (QUrl &url : urls) {
            url = ClipboardUrlParser::normalizePotentialLocalFileUrl(url);
        }

        const bool allLocalFiles = std::all_of(urls.begin(), urls.end(),
            [](const QUrl &url) { return url.isLocalFile(); });
        if (allLocalFiles) {
            return urls;
        }

        const QString rawText = mimeData_->hasText() ? mimeData_->text() : QString();
        if (ClipboardUrlParser::textMatchesUrls(rawText, urls) || (!mimeData_->hasText() && !mimeData_->hasHtml())) {
            return urls;
        }
    }

    if (mimeData_->hasFormat(QStringLiteral("x-special/gnome-copied-files"))) {
        const QList<QUrl> parsed = ClipboardUrlParser::parseProtocolTextUrls(
            QString::fromUtf8(mimeData_->data(QStringLiteral("x-special/gnome-copied-files"))),
            true);
        if (!parsed.isEmpty()) {
            return parsed;
        }
    }

    if (mimeData_->hasFormat(QStringLiteral("application/x-qt-windows-mime;value=\"FileNameW\""))) {
        const QList<QUrl> parsed = ClipboardUrlParser::parseWindowsFileNameMime(
            mimeData_->data(QStringLiteral("application/x-qt-windows-mime;value=\"FileNameW\"")),
            true);
        if (!parsed.isEmpty()) {
            return parsed;
        }
    }

    if (mimeData_->hasFormat(QStringLiteral("application/x-qt-windows-mime;value=\"FileName\""))) {
        const QList<QUrl> parsed = ClipboardUrlParser::parseWindowsFileNameMime(
            mimeData_->data(QStringLiteral("application/x-qt-windows-mime;value=\"FileName\"")),
            false);
        if (!parsed.isEmpty()) {
            return parsed;
        }
    }

    if (mimeData_->hasFormat(QStringLiteral("text/uri-list"))) {
        const QList<QUrl> parsed = ClipboardUrlParser::parseUriListText(
            QString::fromUtf8(mimeData_->data(QStringLiteral("text/uri-list"))));
        if (!parsed.isEmpty()) {
            return parsed;
        }
    }

    if (mimeData_->hasFormat(QStringLiteral("application/x-qt-windows-mime;value=\"UniformResourceLocatorW\""))) {
        const QList<QUrl> parsed = ClipboardUrlParser::parseWindowsUrlMime(
            mimeData_->data(QStringLiteral("application/x-qt-windows-mime;value=\"UniformResourceLocatorW\"")),
            true);
        if (!parsed.isEmpty()) {
            return parsed;
        }
    }

    if (mimeData_->hasFormat(QStringLiteral("application/x-qt-windows-mime;value=\"UniformResourceLocator\""))) {
        const QList<QUrl> parsed = ClipboardUrlParser::parseWindowsUrlMime(
            mimeData_->data(QStringLiteral("application/x-qt-windows-mime;value=\"UniformResourceLocator\"")),
            false);
        if (!parsed.isEmpty()) {
            return parsed;
        }
    }

    const QString text = mimeData_->text();
    if (text.contains(QStringLiteral("x-special/nautilus-clipboard"))) {
        return ClipboardUrlParser::parseProtocolTextUrls(text, false);
    }

    const QStringList plainFormats = {
        QStringLiteral("text/plain;charset=utf-8"),
        QStringLiteral("text/plain")
    };
    for (const QString &format : plainFormats) {
        if (!mimeData_->hasFormat(format)) {
            continue;
        }
        const QString rawText = QString::fromUtf8(mimeData_->data(format));
        if (rawText.contains(QStringLiteral("x-special/nautilus-clipboard"))) {
            const QList<QUrl> parsed = ClipboardUrlParser::parseProtocolTextUrls(rawText, false);
            if (!parsed.isEmpty()) {
                return parsed;
            }
        }
    }

    return {};
}

// --- Text building ---

QString ClipboardItem::buildNormalizedText() const {
    const QList<QUrl> normalizedUrls = getNormalizedUrls();
    if (!normalizedUrls.isEmpty()) {
        QStringList lines;
        for (const QUrl &url : normalizedUrls) {
            lines << (url.isLocalFile() ? url.toLocalFile() : url.toString());
        }
        return lines.join(QLatin1Char('\n'));
    }

    if (mimeData_ && mimeData_->hasText()) {
        return mimeData_->text();
    }

    if (mimeData_ && mimeData_->hasHtml()) {
        // Strip HTML tags to get plain text without triggering
        // QTextDocument's resource loading (remote images, etc.).
        QString text = mimeData_->html();
        static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
        text.replace(tagRe, QString());
        text = text.trimmed();
        // Decode common HTML entities
        text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
        return text;
    }

    return {};
}

QString ClipboardItem::buildSearchableText() const {
    if (!mimeData_) {
        return {};
    }

    QStringList parts;
    if (!alias_.isEmpty()) {
        parts << alias_;
    }
    if (!title_.isEmpty()) {
        parts << title_;
    }
    if (!url_.isEmpty()) {
        parts << url_;
    }
    const QString normalizedText = buildNormalizedText();
    if (!normalizedText.isEmpty()) {
        parts << normalizedText;
    }
    if (mimeData_->hasHtml()) {
        QString htmlText = mimeData_->html();
        static const QRegularExpression tagRe2(QStringLiteral("<[^>]*>"));
        htmlText.replace(tagRe2, QString());
        parts << htmlText.trimmed();
    }
    const QList<QUrl> normalizedUrls = getNormalizedUrls();
    if (!normalizedUrls.isEmpty()) {
        for (const QUrl &url : normalizedUrls) {
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

// --- Fingerprinting ---

QByteArray ClipboardItem::buildFingerprint() const {
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArray::number(static_cast<int>(getContentType())));

    if (!mimeData_) {
        return hash.result();
    }

    const QString normalizedText = getNormalizedText();
    if (!normalizedText.isEmpty()) {
        hash.addData(normalizedText.simplified().toUtf8());
    } else if (mimeData_->hasHtml()) {
        QString htmlText = mimeData_->html();
        static const QRegularExpression tagRe3(QStringLiteral("<[^>]*>"));
        htmlText.replace(tagRe3, QString());
        hash.addData(htmlText.simplified().toUtf8());
    }

    if (shouldTreatHtmlPayloadAsImage()) {
        const QString imageIdentity = htmlImageIdentity();
        if (!imageIdentity.isEmpty()) {
            hash.addData(QByteArrayLiteral("html-image:"));
            hash.addData(imageIdentity.toUtf8());
        }
    }

    const QList<QUrl> normalizedUrls = getNormalizedUrls();
    if (!normalizedUrls.isEmpty()) {
        for (const QUrl &url : normalizedUrls) {
            hash.addData(url.toString(QUrl::FullyEncoded).toUtf8());
            hash.addData(QByteArrayLiteral("\n"));
        }
    }

    if (mimeData_->hasColor()) {
        hash.addData(QByteArray::number(static_cast<quint32>(getColor().rgba())));
    }

    if (hasFastImagePayload()) {
        QByteArray imageBytes = extractFastImagePayloadBytes();

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
        && !(hasFastImagePayload() || (mimeDataLoaded_ && hasDecodableImage())) && !mimeData_->hasColor()) {
        for (const QString &format : mimeData_->formats()) {
            hash.addData(format.toUtf8());
            hash.addData(mimeData_->data(format));
        }
    }

    return hash.result();
}

// --- Image retrieval ---

QPixmap ClipboardItem::getImage() const {
    if (!mimeDataLoaded_ && !thumbnail_.isNull()) {
        return thumbnail_;
    }

    if (imageCache_) {
        return *imageCache_;
    }

    if (!mimeData_) {
        imageCache_ = QPixmap();
        return *imageCache_;
    }

    const QStringList formats = mimeData_->formats();
    for (const QString &format : formats) {
        if (format.startsWith("application/x-qt-windows-mime;value=\"")) {
            QPixmap pixmap;
            QByteArray data = mimeData_->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                imageCache_ = pixmap;
                return *imageCache_;
            }
        }
    }

    for (const QString &format : ContentClassifier::commonImageFormats()) {
        if (mimeData_->hasFormat(format)) {
            QPixmap pixmap;
            QByteArray data = mimeData_->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                imageCache_ = pixmap;
                return *imageCache_;
            }
        }
    }

    for (const QString &format : formats) {
        if (shouldSkipImageDecodeFormat(format)) {
            continue;
        }

        QPixmap pixmap;
        const QByteArray data = mimeData_->data(format);
        if (!data.isEmpty() && pixmap.loadFromData(data)) {
            imageCache_ = pixmap;
            return *imageCache_;
        }
    }

    QVariant imageData = mimeData_->imageData();
    if (imageData.canConvert<QPixmap>()) {
        QPixmap pixmap = qvariant_cast<QPixmap>(imageData);
        if (!pixmap.isNull()) {
            imageCache_ = pixmap;
            return *imageCache_;
        }
    }
    if (imageData.canConvert<QImage>()) {
        QImage img = qvariant_cast<QImage>(imageData);
        if (!img.isNull()) {
            imageCache_ = QPixmap::fromImage(img);
            return *imageCache_;
        }
    }

    imageCache_ = QPixmap();
    return *imageCache_;
}

// --- Fast image payload extraction (instance method) ---

QByteArray ClipboardItem::extractFastImagePayloadBytes(QString *formatOut) const {
    if (!mimeData_) {
        return {};
    }
    if (formatOut) {
        formatOut->clear();
    }

    for (const QString &format : ContentClassifier::preferredImageFormats()) {
        if (mimeData_->hasFormat(format)) {
            const QByteArray data = mimeData_->data(format);
            if (!data.isEmpty()) {
                if (formatOut) {
                    *formatOut = format;
                }
                return data;
            }
        }
    }

    const QStringList formats = mimeData_->formats();
    for (const QString &format : formats) {
        const QString lower = format.toLower();
        const bool imageMime = lower.startsWith(QStringLiteral("image/"));
        const bool imageWindowsMime = lower.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))
            && (lower.contains(QStringLiteral("png"))
                || lower.contains(QStringLiteral("jpeg"))
                || lower.contains(QStringLiteral("jpg"))
                || lower.contains(QStringLiteral("bmp"))
                || lower.contains(QStringLiteral("dib"))
                || lower.contains(QStringLiteral("gif"))
                || lower.contains(QStringLiteral("webp")));
        if (imageMime || imageWindowsMime) {
            const QByteArray data = mimeData_->data(format);
            if (!data.isEmpty()) {
                if (formatOut) {
                    *formatOut = format;
                }
                return data;
            }
        }
    }

    return {};
}

// --- Comparison ---

bool ClipboardItem::operator==(const ClipboardItem &other) const {
    if (!mimeData_ && !other.mimeData_) {
        return true;
    }
    if (!mimeData_ || !other.mimeData_) {
        // When either item is light-loaded (no MIME data), compare via fingerprint
        if (fingerprintCache_ && other.fingerprintCache_) {
            return *fingerprintCache_ == *other.fingerprintCache_;
        }
        if (!mimeDataLoaded_ || !other.mimeDataLoaded_) {
            return fingerprint() == other.fingerprint();
        }
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
        const QString html1 = mimeData_->html();
        const QString html2 = other.mimeData_->html();
        QStringView frag1 = htmlFragment(html1);
        QStringView frag2 = htmlFragment(html2);
        const bool imageLikeHtml = shouldTreatHtmlPayloadAsImage() || other.shouldTreatHtmlPayloadAsImage();
        if (imageLikeHtml) {
            const QString imageId1 = htmlImageIdentity();
            const QString imageId2 = other.htmlImageIdentity();
            if (!imageId1.isEmpty() || !imageId2.isEmpty()) {
                if (imageId1 != imageId2) {
                    return false;
                }
            } else if (frag1 != frag2) {
                return false;
            }
        } else if (frag1 != frag2) {
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

    if (hasDecodableImage() || other.hasDecodableImage()) {
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

// --- Copy constructor ---

ClipboardItem::ClipboardItem(const ClipboardItem &other) : icon_(other.icon_) {
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
    alias_ = other.alias_;
    pinned_ = other.pinned_;
    icon_ = other.icon_;
    fingerprintCache_ = other.fingerprintCache_;
    searchableTextCache_ = other.searchableTextCache_;
    normalizedUrlsCache_ = other.normalizedUrlsCache_;
    imageCache_ = other.imageCache_;
    imageSizeCache_ = other.imageSizeCache_;
    thumbnail_ = other.thumbnail_;
    thumbnailAvailableHint_ = other.thumbnailAvailableHint_;
    sourceFilePath_ = other.sourceFilePath_;
    mimeDataFileOffset_ = other.mimeDataFileOffset_;
    mimeDataLoaded_ = other.mimeDataLoaded_;
    mimeDataLoader_ = other.mimeDataLoader_;
    cachedContentType_ = other.cachedContentType_;
    cachedPreviewKind_ = other.cachedPreviewKind_;
    cachedNormalizedText_ = other.cachedNormalizedText_;
    cachedNormalizedUrls_ = other.cachedNormalizedUrls_;
}

// --- Copy assignment ---

ClipboardItem& ClipboardItem::operator=(const ClipboardItem &other) {
    if (this != &other) {
        time_ = other.time_;
        name_ = other.name_;
        favicon_ = other.favicon_;
        title_ = other.title_;
        url_ = other.url_;
        alias_ = other.alias_;
        pinned_ = other.pinned_;
        icon_ = other.icon_;
        fingerprintCache_ = other.fingerprintCache_;
        searchableTextCache_ = other.searchableTextCache_;
        normalizedUrlsCache_ = other.normalizedUrlsCache_;
        imageCache_ = other.imageCache_;
        imageSizeCache_ = other.imageSizeCache_;
        thumbnail_ = other.thumbnail_;
        thumbnailAvailableHint_ = other.thumbnailAvailableHint_;
        sourceFilePath_ = other.sourceFilePath_;
        mimeDataFileOffset_ = other.mimeDataFileOffset_;
        mimeDataLoaded_ = other.mimeDataLoaded_;
        mimeDataLoader_ = other.mimeDataLoader_;
        cachedContentType_ = other.cachedContentType_;
        cachedPreviewKind_ = other.cachedPreviewKind_;
        cachedNormalizedText_ = other.cachedNormalizedText_;
        cachedNormalizedUrls_ = other.cachedNormalizedUrls_;

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

// --- createLightweight ---

ClipboardItem ClipboardItem::createLightweight(const QPixmap &icon, const QMimeData *mimeData) {
    ClipboardItem item;
    item.icon_ = icon;
    if (mimeData) {
        item.mimeData_.reset(new QMimeData);
        if (mimeData->hasText()) {
            item.mimeData_->setText(mimeData->text());
        }
        if (mimeData->hasHtml()) {
            item.mimeData_->setHtml(mimeData->html());
        }
        if (mimeData->hasUrls()) {
            item.mimeData_->setUrls(mimeData->urls());
        }
        if (mimeData->hasColor()) {
            item.mimeData_->setColorData(mimeData->colorData());
        }

        // Copy additional MIME formats needed for paste fidelity
        // (RTF, Office shapes, etc.).  Skip formats already captured
        // above and JVM formats that trigger expensive cross-process IPC.
        for (const QString &format : mimeData->formats()) {
            if (format == QStringLiteral("text/plain")
                || format == QStringLiteral("text/html")
                || format == QStringLiteral("text/uri-list")) {
                continue;
            }
            if (!shouldCopyLightMimeFormat(format)) {
                continue;
            }
            const QByteArray data = mimeData->data(format);
            if (!data.isEmpty()) {
                item.mimeData_->setData(format, data);
            }
        }

        // Capture image payload: try fast raw bytes first, fall back
        // to QVariant image decoding for screenshots.
        QString imageFormat;
        const QByteArray imageBytes = ClipboardImageDecoder::extractFastImagePayloadBytes(mimeData, &imageFormat);
        if (!imageBytes.isEmpty()) {
            const QString format = imageFormat.isEmpty()
                ? QStringLiteral("application/x-qt-image")
                : imageFormat;
            item.mimeData_->setData(format, imageBytes);
        } else if (mimeData->hasImage()) {
            const QPixmap pixmap = ClipboardImageDecoder::decodePixmapFromMimeData(mimeData);
            ClipboardImageDecoder::materializeCanonicalImage(item.mimeData_.data(), pixmap);
        }
    }
    item.time_ = QDateTime::currentDateTime();
    item.name_ = QString::number(item.time_.toMSecsSinceEpoch());
    item.cachedNormalizedText_ = item.buildNormalizedText();
    item.cachedNormalizedUrls_ = item.buildNormalizedUrls();
    item.cachedContentType_ = mimeData
        ? ContentClassifier::classifyLight(mimeData, item.cachedNormalizedText_, item.cachedNormalizedUrls_)
        : item.detectLightContentType();
    item.cachedPreviewKind_ = PreviewClassifier::classifyLight(item.cachedContentType_,
                                                               item.cachedNormalizedText_,
                                                               item.hasThumbnailHint(),
                                                               item.mimeData_.data(),
                                                               item.hasFastImagePayload());
    item.mimeDataLoaded_ = false;
    return item;
}

// --- Search ---

bool ClipboardItem::contains(const QString &keyword) const {
    if (keyword.isEmpty()) {
        return false;
    }

    const QString lower = keyword.toLower();

    if (!mimeDataLoaded_) {
        return cachedNormalizedText_.toLower().contains(lower)
            || title_.toLower().contains(lower)
            || url_.toLower().contains(lower)
            || alias_.toLower().contains(lower);
    }

    if (!mimeData_) {
        return false;
    }

    if (!searchableTextCache_) {
        searchableTextCache_ = buildSearchableText();
    }

    return searchableTextCache_->contains(lower);
}

// --- HTML helpers ---

QString ClipboardItem::htmlImageIdentity() const {
    if (!shouldTreatHtmlPayloadAsImage()) {
        return {};
    }

    const QString html = mimeData_->html();

    static const QRegularExpression objectHashRegex(
        QStringLiteral(R"(/attach/object/([A-Za-z0-9_-]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch objectHashMatch = objectHashRegex.match(html);
    if (objectHashMatch.hasMatch()) {
        return objectHashMatch.captured(1);
    }

    static const QRegularExpression srcRegex(
        QStringLiteral(R"(<img[^>]+src\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch srcMatch = srcRegex.match(html);
    if (srcMatch.hasMatch()) {
        QUrl url(srcMatch.captured(1).trimmed());
        if (url.isValid()) {
            QString normalized = url.toString(QUrl::RemoveQuery | QUrl::FullyEncoded);
            if (!normalized.isEmpty()) {
                return normalized;
            }
        }
        return srcMatch.captured(1).trimmed();
    }

    static const QRegularExpression idRegex(
        QStringLiteral(R"(ksDocClipboardId:'\{([^']+)\}')"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch idMatch = idRegex.match(html);
    if (idMatch.hasMatch()) {
        return idMatch.captured(1);
    }

    return htmlFragment(html).toString().simplified();
}

QStringView ClipboardItem::htmlFragment(const QString &html) {
    static const QString startMarker = QStringLiteral("<!--StartFragment-->");
    static const QString endMarker = QStringLiteral("<!--EndFragment-->");
    int start = html.indexOf(startMarker);
    int end = html.indexOf(endMarker);
    if (start >= 0 && end > start) {
        return QStringView(html).mid(start + startMarker.length(), end - start - startMarker.length());
    }
    return QStringView(html);
}

// --- MIME format filtering ---

bool ClipboardItem::shouldCopyLightMimeFormat(const QString &format) {
    const QString lower = format.toLower();
    if (lower.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))) {
        // Skip JVM clipboard formats — extracting them triggers expensive
        // cross-process IPC (COM/OLE -> JVM serialization) and the data is
        // editor-internal state (IntelliJ folding, paste options) that is
        // not useful for later paste.
        if (lower.contains(QStringLiteral("java_dataflavor"))
            || lower.contains(QStringLiteral("x-java-"))
            || lower.contains(QStringLiteral("chromium internal"))
            || lower.contains(QStringLiteral("chromium web custom"))) {
            return false;
        }
        // Preserve Windows clipboard formats (EMF/OLE/Office) so Office shapes remain editable.
        return true;
    }
    return lower.startsWith(QStringLiteral("text/"))
        || lower.contains(QStringLiteral("html"))
        || lower.contains(QStringLiteral("plain"))
        || lower.contains(QStringLiteral("xml"))
        || lower.contains(QStringLiteral("json"))
        || lower.contains(QStringLiteral("url"))
        || lower.contains(QStringLiteral("uri"))
        || lower.contains(QStringLiteral("descriptor"))
        || lower.contains(QStringLiteral("ksdocclipboard"))
        || lower.contains(QStringLiteral("rich text"));
}

bool ClipboardItem::shouldCopyExtraMimeFormat(const QString &format) {
    return shouldCopyLightMimeFormat(format);
}
