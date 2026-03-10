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
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QPixmap>
#include <QRegularExpression>
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
    mutable QList<QUrl> normalizedUrlsCache_;
    mutable bool normalizedUrlsCacheInitialized_ = false;

    static QList<QUrl> parseUrlsFromLines(const QStringList &lines, bool strictMode) {
        QList<QUrl> result;
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }

            QUrl url(trimmed);
            if (url.isValid() && !url.isRelative() && !trimmed.contains(QLatin1Char(' '))) {
                result << url;
                continue;
            }

            if ((trimmed.startsWith(QLatin1String("/"))
                 || (trimmed.size() > 2 && trimmed[1] == QLatin1Char(':')
                     && (trimmed[2] == QLatin1Char('/') || trimmed[2] == QLatin1Char('\\'))))
                && QFileInfo::exists(trimmed)) {
                result << QUrl::fromLocalFile(trimmed);
                continue;
            }

            if (strictMode) {
                return {};
            }
        }
        return result;
    }

    static QList<QUrl> parseProtocolTextUrls(const QString &text, bool allowOperationOnlyHeader) {
        if (text.isEmpty()) {
            return {};
        }

        QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
        if (lines.isEmpty()) {
            return {};
        }

        bool protocolHeader = false;
        QString first = lines.first().trimmed();
        if (first == QLatin1String("x-special/nautilus-clipboard")) {
            protocolHeader = true;
            lines.removeFirst();
            if (!lines.isEmpty()) {
                const QString op = lines.first().trimmed().toLower();
                if (op == QLatin1String("copy") || op == QLatin1String("cut")) {
                    lines.removeFirst();
                }
            }
        } else if (allowOperationOnlyHeader
                   && (first.compare(QLatin1String("copy"), Qt::CaseInsensitive) == 0
                       || first.compare(QLatin1String("cut"), Qt::CaseInsensitive) == 0)) {
            protocolHeader = true;
            lines.removeFirst();
        }

        QList<QUrl> parsed = parseUrlsFromLines(lines, protocolHeader);
        if (!parsed.isEmpty()) {
            return parsed;
        }

        return {};
    }

    static QList<QUrl> parseUriListText(const QString &text) {
        if (text.isEmpty()) {
            return {};
        }

        QStringList filteredLines;
        const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (!trimmed.startsWith(QLatin1Char('#'))) {
                filteredLines << trimmed;
            }
        }

        return parseUrlsFromLines(filteredLines, true);
    }

    static QString decodeNullTerminatedText(const QByteArray &data, bool utf16) {
        if (data.isEmpty()) {
            return {};
        }

        QString text = utf16
            ? QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData()), data.size() / 2)
            : QString::fromUtf8(data);

        const int nullIndex = text.indexOf(QChar('\0'));
        if (nullIndex >= 0) {
            text.truncate(nullIndex);
        }
        return text.trimmed();
    }

    static QList<QUrl> parseWindowsUrlMime(const QByteArray &data, bool utf16) {
        const QString text = decodeNullTerminatedText(data, utf16);
        if (text.isEmpty()) {
            return {};
        }

        const QUrl url(text);
        if (url.isValid() && !url.isRelative()) {
            return {url};
        }
        return {};
    }

    static QList<QUrl> parseWindowsFileNameMime(const QByteArray &data, bool utf16) {
        if (data.isEmpty()) {
            return {};
        }

        QString text = utf16
            ? QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData()), data.size() / 2)
            : QString::fromUtf8(data);

        const QStringList parts = text.split(QChar('\0'), Qt::SkipEmptyParts);
        QList<QUrl> result;
        for (const QString &part : parts) {
            const QString trimmed = part.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            result << QUrl::fromLocalFile(trimmed);
        }
        return result;
    }

    static bool textMatchesUrls(const QString &text, const QList<QUrl> &urls) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) {
            return true;
        }

        auto candidatesForUrl = [](const QUrl &url) {
            QStringList candidates;
            candidates << url.toString(QUrl::FullyEncoded);
            candidates << QUrl::fromPercentEncoding(url.toEncoded());
            if (url.isLocalFile()) {
                candidates << url.toLocalFile();
            }
            candidates.removeAll(QString());
            candidates.removeDuplicates();
            return candidates;
        };

        if (urls.size() == 1) {
            return candidatesForUrl(urls.first()).contains(trimmed);
        }

        const QStringList lines = trimmed.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
        if (lines.size() != urls.size()) {
            return false;
        }

        for (int i = 0; i < urls.size(); ++i) {
            const QString line = lines[i].trimmed();
            if (!candidatesForUrl(urls[i]).contains(line)) {
                return false;
            }
        }
        return true;
    }

    QList<QUrl> buildNormalizedUrls() const {
        if (!mimeData_) {
            return {};
        }

        QList<QUrl> urls = mimeData_->urls();
        if (!urls.isEmpty()) {
            const bool allLocalFiles = std::all_of(urls.begin(), urls.end(),
                [](const QUrl &url) { return url.isLocalFile(); });
            if (allLocalFiles) {
                return urls;
            }

            const QString rawText = mimeData_->hasText() ? mimeData_->text() : QString();
            if (textMatchesUrls(rawText, urls) || (!mimeData_->hasText() && !mimeData_->hasHtml())) {
                return urls;
            }
        }

        if (mimeData_->hasFormat(QStringLiteral("x-special/gnome-copied-files"))) {
            const QList<QUrl> parsed = parseProtocolTextUrls(
                QString::fromUtf8(mimeData_->data(QStringLiteral("x-special/gnome-copied-files"))),
                true);
            if (!parsed.isEmpty()) {
                return parsed;
            }
        }

        if (mimeData_->hasFormat(QStringLiteral("application/x-qt-windows-mime;value=\"FileNameW\""))) {
            const QList<QUrl> parsed = parseWindowsFileNameMime(
                mimeData_->data(QStringLiteral("application/x-qt-windows-mime;value=\"FileNameW\"")),
                true);
            if (!parsed.isEmpty()) {
                return parsed;
            }
        }

        if (mimeData_->hasFormat(QStringLiteral("application/x-qt-windows-mime;value=\"FileName\""))) {
            const QList<QUrl> parsed = parseWindowsFileNameMime(
                mimeData_->data(QStringLiteral("application/x-qt-windows-mime;value=\"FileName\"")),
                false);
            if (!parsed.isEmpty()) {
                return parsed;
            }
        }

        if (mimeData_->hasFormat(QStringLiteral("text/uri-list"))) {
            const QList<QUrl> parsed = parseUriListText(
                QString::fromUtf8(mimeData_->data(QStringLiteral("text/uri-list"))));
            if (!parsed.isEmpty()) {
                return parsed;
            }
        }

        if (mimeData_->hasFormat(QStringLiteral("application/x-qt-windows-mime;value=\"UniformResourceLocatorW\""))) {
            const QList<QUrl> parsed = parseWindowsUrlMime(
                mimeData_->data(QStringLiteral("application/x-qt-windows-mime;value=\"UniformResourceLocatorW\"")),
                true);
            if (!parsed.isEmpty()) {
                return parsed;
            }
        }

        if (mimeData_->hasFormat(QStringLiteral("application/x-qt-windows-mime;value=\"UniformResourceLocator\""))) {
            const QList<QUrl> parsed = parseWindowsUrlMime(
                mimeData_->data(QStringLiteral("application/x-qt-windows-mime;value=\"UniformResourceLocator\"")),
                false);
            if (!parsed.isEmpty()) {
                return parsed;
            }
        }

        const QString text = mimeData_->text();
        if (text.contains(QStringLiteral("x-special/nautilus-clipboard"))) {
            return parseProtocolTextUrls(text, false);
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
                const QList<QUrl> parsed = parseProtocolTextUrls(rawText, false);
                if (!parsed.isEmpty()) {
                    return parsed;
                }
            }
        }

        return {};
    }

    QString buildNormalizedText() const {
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
            QTextDocument doc;
            doc.setHtml(mimeData_->html());
            return doc.toPlainText();
        }

        return {};
    }


    static bool shouldSkipImageDecodeFormatName(const QString &format) {
        const QString lower = format.toLower();
        return lower.startsWith(QStringLiteral("text/"))
            || lower.contains(QStringLiteral("html"))
            || lower.contains(QStringLiteral("plain"))
            || lower.contains(QStringLiteral("xml"))
            || lower.contains(QStringLiteral("json"))
            || lower.contains(QStringLiteral("url"))
            || lower.contains(QStringLiteral("uri"))
            || lower.contains(QStringLiteral("descriptor"))
            || lower.contains(QStringLiteral("gvml"))
            || lower.contains(QStringLiteral("rtf"))
            || lower.contains(QStringLiteral("rich text"));
    }

    static QPixmap decodePixmapFromMimeData(const QMimeData *mimeData) {
        if (!mimeData) {
            return QPixmap();
        }

        const QStringList formats = mimeData->formats();
        for (const QString &format : formats) {
            if (format.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))) {
                QPixmap pixmap;
                const QByteArray data = mimeData->data(format);
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
            if (mimeData->hasFormat(format)) {
                QPixmap pixmap;
                const QByteArray data = mimeData->data(format);
                if (!data.isEmpty() && pixmap.loadFromData(data)) {
                    return pixmap;
                }
            }
        }

        for (const QString &format : formats) {
            if (shouldSkipImageDecodeFormatName(format)) {
                continue;
            }

            QPixmap pixmap;
            const QByteArray data = mimeData->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                return pixmap;
            }
        }

        const QVariant imageData = mimeData->imageData();
        if (imageData.canConvert<QPixmap>()) {
            const QPixmap pixmap = qvariant_cast<QPixmap>(imageData);
            if (!pixmap.isNull()) {
                return pixmap;
            }
        }
        if (imageData.canConvert<QImage>()) {
            const QImage image = qvariant_cast<QImage>(imageData);
            if (!image.isNull()) {
                return QPixmap::fromImage(image);
            }
        }

        return QPixmap();
    }

    static void materializeCanonicalImage(QMimeData *mimeData, const QPixmap &pixmap) {
        if (!mimeData || pixmap.isNull()) {
            return;
        }

        QByteArray imageData;
        QBuffer buffer(&imageData);
        if (!buffer.open(QIODevice::WriteOnly)) {
            return;
        }
        if (!pixmap.save(&buffer, "PNG")) {
            return;
        }

        mimeData->setData(QStringLiteral("application/x-qt-image"), imageData);
        mimeData->setData(QStringLiteral("image/png"), imageData);
        mimeData->setData(QStringLiteral("application/x-qt-windows-mime;value=\"PNG\""), imageData);
    }

    bool isImageLikeText(const QString &text) const {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) {
            return true;
        }

        if (trimmed.size() == 1) {
            const QChar ch = trimmed.at(0);
            if (ch == QChar(0xFFFC) || ch == QChar::ReplacementCharacter) {
                return true;
            }
        }

        return false;
    }

    bool hasDecodableImage() const {
        if (!mimeData_) {
            return false;
        }

        if (mimeData_->hasImage()) {
            return true;
        }

        const QStringList windowsFormats = mimeData_->formats();
        for (const QString &format : windowsFormats) {
            if (format.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))) {
                QPixmap pixmap;
                const QByteArray data = mimeData_->data(format);
                if (!data.isEmpty() && pixmap.loadFromData(data)) {
                    return true;
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
                const QByteArray data = mimeData_->data(format);
                if (!data.isEmpty() && pixmap.loadFromData(data)) {
                    return true;
                }
            }
        }

        for (const QString &format : mimeData_->formats()) {
            if (shouldSkipImageDecodeFormat(format)) {
                continue;
            }

            QPixmap pixmap;
            const QByteArray data = mimeData_->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                return true;
            }
        }

        const QVariant imageData = mimeData_->imageData();
        if (imageData.canConvert<QPixmap>()) {
            const QPixmap pixmap = qvariant_cast<QPixmap>(imageData);
            if (!pixmap.isNull()) {
                return true;
            }
        }
        if (imageData.canConvert<QImage>()) {
            const QImage image = qvariant_cast<QImage>(imageData);
            if (!image.isNull()) {
                return true;
            }
        }

        return false;
    }

    bool hasMaterializedImagePayload() const {
        if (!mimeData_) {
            return false;
        }

        if (mimeData_->hasImage()) {
            return true;
        }

        static const QStringList preferredFormats = {
            QStringLiteral("application/x-qt-image"),
            QStringLiteral("image/png"),
            QStringLiteral("image/jpeg"),
            QStringLiteral("image/gif"),
            QStringLiteral("image/bmp")
        };

        for (const QString &format : preferredFormats) {
            if (!mimeData_->hasFormat(format)) {
                continue;
            }

            QPixmap pixmap;
            const QByteArray data = mimeData_->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                return true;
            }
        }

        const QStringList formats = mimeData_->formats();
        for (const QString &format : formats) {
            if (!format.startsWith(QStringLiteral("application/x-qt-windows-mime;value=\""))) {
                continue;
            }

            QPixmap pixmap;
            const QByteArray data = mimeData_->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                return true;
            }
        }

        return false;
    }

    bool shouldSkipImageDecodeFormat(const QString &format) const {
        const QString lower = format.toLower();
        return lower.startsWith(QStringLiteral("text/"))
            || lower.contains(QStringLiteral("html"))
            || lower.contains(QStringLiteral("plain"))
            || lower.contains(QStringLiteral("xml"))
            || lower.contains(QStringLiteral("json"))
            || lower.contains(QStringLiteral("url"))
            || lower.contains(QStringLiteral("uri"))
            || lower.contains(QStringLiteral("descriptor"))
            || lower.contains(QStringLiteral("gvml"))
            || lower.contains(QStringLiteral("rtf"))
            || lower.contains(QStringLiteral("rich text"));
    }

    bool shouldTreatHtmlPayloadAsImage() const {
        if (!mimeData_ || !mimeData_->hasHtml()) {
            return false;
        }

        const QString text = buildNormalizedText();
        const QString html = mimeData_->html();
        const QString lowerHtml = html.toLower();
        if (!lowerHtml.contains(QStringLiteral("<img"))) {
            return false;
        }

        QTextDocument doc;
        doc.setHtml(html);
        const QString plainText = doc.toPlainText();
        if (!isImageLikeText(text) && !isImageLikeText(plainText)) {
            return false;
        }

        return lowerHtml.contains(QStringLiteral("ksdocclipboard"))
            || lowerHtml.contains(QStringLiteral("kingsoft"))
            || lowerHtml.contains(QStringLiteral("from:'wps'"))
            || lowerHtml.contains(QStringLiteral("from:&quot;wps&quot;"))
            || hasDecodableImage();
    }

    QString htmlImageIdentity() const {
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
        const QString normalizedText = buildNormalizedText();
        if (!normalizedText.isEmpty()) {
            parts << normalizedText;
        }
        if (mimeData_->hasHtml()) {
            QTextDocument doc;
            doc.setHtml(mimeData_->html());
            parts << doc.toPlainText();
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

    QByteArray buildFingerprint() const {
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(QByteArray::number(static_cast<int>(getContentType())));

        if (!mimeData_) {
            return hash.result();
        }

        const QString normalizedText = buildNormalizedText();
        if (!normalizedText.isEmpty()) {
            hash.addData(normalizedText.simplified().toUtf8());
        } else if (mimeData_->hasHtml()) {
            QTextDocument doc;
            doc.setHtml(mimeData_->html());
            hash.addData(doc.toPlainText().simplified().toUtf8());
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

        if (hasDecodableImage()) {
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
            && !hasDecodableImage() && !mimeData_->hasColor()) {
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
        normalizedUrlsCacheInitialized_ = false;
        normalizedUrlsCache_.clear();
    }

public:
    ClipboardItem(const QPixmap &icon, const QMimeData *mimeData) : icon_(icon) {
        if (mimeData) {
            mimeData_.reset(new QMimeData);

            for (const QString &format : mimeData->formats()) {
                mimeData_->setData(format, mimeData->data(format));
            }

            materializeCanonicalImage(mimeData_.data(), decodePixmapFromMimeData(mimeData));

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
        normalizedUrlsCache_ = other.normalizedUrlsCache_;
        normalizedUrlsCacheInitialized_ = other.normalizedUrlsCacheInitialized_;
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
            normalizedUrlsCache_ = other.normalizedUrlsCache_;
            normalizedUrlsCacheInitialized_ = other.normalizedUrlsCacheInitialized_;

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
    QString getNormalizedText() const { return buildNormalizedText(); }
    QList<QUrl> getNormalizedUrls() const {
        if (!normalizedUrlsCacheInitialized_) {
            normalizedUrlsCache_ = buildNormalizedUrls();
            normalizedUrlsCacheInitialized_ = true;
        }
        return normalizedUrlsCache_;
    }

    QPixmap getImage() const {
        if (!mimeData_) {
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

        for (const QString &format : formats) {
            if (shouldSkipImageDecodeFormat(format)) {
                continue;
            }

            QPixmap pixmap;
            const QByteArray data = mimeData_->data(format);
            if (!data.isEmpty() && pixmap.loadFromData(data)) {
                return pixmap;
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

        if (hasMaterializedImagePayload()) {
            return Image;
        }

        QList<QUrl> urls = getNormalizedUrls();
        if (!urls.isEmpty() && !mimeData_->hasHtml()) {
            bool allValid = !urls.isEmpty() && std::all_of(urls.begin(), urls.end(),
                [](const QUrl &url) { return url.isValid() && !url.isRelative(); });
            if (allValid) {
                bool allFiles = std::all_of(urls.begin(), urls.end(),
                    [](const QUrl &url) { return url.isLocalFile(); });
                return allFiles ? File : Link;
            }
            return Text;
        }

        if (shouldTreatHtmlPayloadAsImage()) {
            return Image;
        }

        if (hasDecodableImage() && !mimeData_->hasHtml()) {
            return Image;
        }

        if (mimeData_->hasHtml()) {
            QString text = buildNormalizedText().trimmed();
            if (!mimeData_->formats().contains("Rich Text Format")
                && !text.contains("\n")
                && (text.startsWith("http://") || text.startsWith("https://"))) {
                return Link;
            }
            return RichText;
        }

        if (hasDecodableImage()) return Image;
        return Text;
    }
};

#endif // MPASTE_CLIPBOARDITEM_H
