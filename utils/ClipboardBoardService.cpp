// input: Depends on ClipboardBoardService.h, LocalSaver, MPasteSettings, and Qt IO/threading utilities.
// output: Implements board persistence, deferred loading, thumbnail processing, and keyword search routines.
// pos: utils layer board service implementation.
// update: If I change, update this header block and my folder README.md.
// note: Thumbnail decode now accepts Qt serialized image payloads, uses shared card preview metrics, respects data-layer preview kind for rich text, trims rich-text margins, backfills missing on-disk thumbnails on demand, and uses bounded worker concurrency.
#include "ClipboardBoardService.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextOption>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QRunnable>

#include "data/CardPreviewMetrics.h"
#include "data/ContentClassifier.h"
#include "data/LocalSaver.h"
#include "utils/MPasteSettings.h"

namespace {

qreal htmlPreviewZoom(qreal devicePixelRatio) {
    return qMax<qreal>(1.0, devicePixelRatio);
}

QSize previewLogicalSize(int itemScale) {
    const int scale = qMax(50, itemScale);
    return QSize(qMax(1, kCardPreviewWidth * scale / 100),
                 qMax(1, kCardPreviewHeight * scale / 100));
}

QString richTextThumbnailStyleSheet() {
    return QStringLiteral(
        "html, body { margin: 0 !important; width: 100% !important; max-width: 100% !important; }"
        "body {"
        " padding: 12px !important;"
        " font-size: 15px !important;"
        " line-height: 1.38 !important;"
        " }"
        "body * {"
        " margin: 0 !important;"
        " max-width: 100% !important;"
        " white-space: normal !important;"
        " overflow-wrap: anywhere !important;"
        " word-wrap: break-word !important;"
        " word-break: break-word !important;"
        "}"
        "table {"
        " width: 100% !important;"
        " max-width: 100% !important;"
        " table-layout: fixed !important;"
        " border-collapse: collapse !important;"
        "}"
        "tr, td, th {"
        " max-width: 100% !important;"
        " white-space: normal !important;"
        " overflow-wrap: anywhere !important;"
        " word-wrap: break-word !important;"
        " word-break: break-word !important;"
        "}"
        "a, span, font, strong, em, b, i, u, sub, sup {"
        " white-space: normal !important;"
        " overflow-wrap: anywhere !important;"
        " word-wrap: break-word !important;"
        " word-break: break-word !important;"
        "}"
        "img { max-width: 100% !important; height: auto !important; }"
        "pre, code {"
        " white-space: pre-wrap !important;"
        " overflow-wrap: anywhere !important;"
        " word-break: break-word !important;"
        "}");
}

QString richTextHtmlForThumbnail(QString html) {
    const QString styleTag = QStringLiteral("<style>%1</style>").arg(richTextThumbnailStyleSheet());
    const QRegularExpression headCloseRegex(QStringLiteral(R"(</head\s*>)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch headCloseMatch = headCloseRegex.match(html);
    if (headCloseMatch.hasMatch()) {
        html.insert(headCloseMatch.capturedStart(), styleTag);
        return html;
    }

    const QRegularExpression headOpenRegex(QStringLiteral(R"(<head[^>]*>)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch headMatch = headOpenRegex.match(html);
    if (headMatch.hasMatch()) {
        html.insert(headMatch.capturedEnd(), styleTag);
        return html;
    }

    const QRegularExpression htmlOpenRegex(QStringLiteral(R"(<html[^>]*>)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch htmlMatch = htmlOpenRegex.match(html);
    if (htmlMatch.hasMatch()) {
        html.insert(htmlMatch.capturedEnd(), QStringLiteral("<head>%1</head>").arg(styleTag));
        return html;
    }

    return QStringLiteral("<html><head>%1</head><body>%2</body></html>").arg(styleTag, html);
}

void configureRichTextThumbnailDocument(QTextDocument &document,
                                        const QString &html,
                                        const QString &imageSource,
                                        const QByteArray &imageBytes) {
    document.setDocumentMargin(0);
    QTextOption option = document.defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(option);
    document.setDefaultStyleSheet(richTextThumbnailStyleSheet());

    if (!imageSource.isEmpty() && !imageBytes.isEmpty()) {
        QImage image;
        if (!image.loadFromData(imageBytes)) {
            image = ContentClassifier::decodeQtSerializedImage(imageBytes);
        }
        if (!image.isNull()) {
            document.addResource(QTextDocument::ImageResource, QUrl(imageSource), image);
        }
    }

    document.setHtml(richTextHtmlForThumbnail(html));
}

qreal maxScreenDevicePixelRatio() {
    qreal dpr = 1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen) {
            dpr = qMax(dpr, screen->devicePixelRatio());
        }
    }
    return dpr;
}

bool isVeryTallImage(const QSize &size) {
    return size.isValid() && size.height() >= qMax(4000, size.width() * 4);
}

ClipboardBoardService::IndexedItemMeta buildIndexedItemMeta(const QString &filePath,
                                                            const ClipboardItem &item) {
    ClipboardBoardService::IndexedItemMeta meta;
    meta.filePath = filePath;
    meta.name = item.getName();
    meta.title = item.getTitle();
    meta.url = item.getUrl();
    meta.alias = item.getAlias();
    meta.normalizedUrls = item.getNormalizedUrls();
    meta.fingerprint = item.fingerprint();
    meta.time = item.getTime();
    meta.contentType = item.getContentType();
    meta.previewKind = item.getPreviewKind();
    meta.mimeDataOffset = item.mimeDataFileOffset();
    meta.pinned = item.isPinned();
    meta.hasThumbnailHint = item.hasThumbnailHint();

    QStringList searchParts;
    if (!meta.alias.isEmpty()) {
        searchParts << meta.alias;
    }
    if (!meta.title.isEmpty()) {
        searchParts << meta.title;
    }
    if (!meta.url.isEmpty()) {
        searchParts << meta.url;
    }
    const QString normalizedText = item.getNormalizedText();
    if (!normalizedText.isEmpty()) {
        // Keep only enough text for typical keyword matching in the index;
        // full-text search falls back to async file-based matching.
        searchParts << normalizedText.left(512);
    }
    for (const QUrl &url : meta.normalizedUrls) {
        searchParts << (url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded));
    }
    meta.searchableText = searchParts.join(QLatin1Char('\n')).toLower();
    return meta;
}

bool indexedItemMatchesFilter(const ClipboardBoardService::IndexedItemMeta &item,
                              ClipboardItem::ContentType type,
                              const QString &keyword,
                              const QSet<QString> &matchedNames) {
    if (item.name.isEmpty()) {
        return false;
    }
    if (type != ClipboardItem::All && item.contentType != type) {
        return false;
    }
    if (keyword.isEmpty()) {
        return true;
    }
    return item.searchableText.contains(keyword, Qt::CaseInsensitive) || matchedNames.contains(item.name);
}

bool richTextHtmlHasImageContent(const QString &html) {
    return !ContentClassifier::firstHtmlImageSource(html).isEmpty()
        || html.contains(QStringLiteral("<img"), Qt::CaseInsensitive);
}

bool isPreviewCacheManagedContent(const ClipboardItem &item) {
    const ClipboardItem::ContentType type = item.getContentType();
    return type == ClipboardItem::Image
        || type == ClipboardItem::Office
        || type == ClipboardItem::RichText;
}

bool usesVisualPreview(const ClipboardItem &item) {
    const ClipboardItem::ContentType type = item.getContentType();
    return type == ClipboardItem::Image
        || type == ClipboardItem::Office
        || (type == ClipboardItem::RichText && item.getPreviewKind() == ClipboardItem::VisualPreview);
}

QImage trimTransparentPadding(const QImage &source, int paddingPx) {
    if (source.isNull()) {
        return {};
    }

    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0) {
        return image;
    }

    int left = width;
    int right = -1;
    int top = height;
    int bottom = -1;
    constexpr int kAlphaThreshold = 6;

    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (qAlpha(line[x]) > kAlphaThreshold) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
        }
    }

    if (right < left || bottom < top) {
        return image;
    }

    left = qMax(0, left - paddingPx);
    right = qMin(width - 1, right + paddingPx);
    top = qMax(0, top - paddingPx);
    bottom = qMin(height - 1, bottom + paddingPx);

    const QRect bounds(left, top, right - left + 1, bottom - top + 1);
    return image.copy(bounds);
}

QImage scaleCropToTarget(const QImage &source, const QSize &pixelTargetSize) {
    if (source.isNull() || !pixelTargetSize.isValid()) {
        return source;
    }

    QImage scaled = source.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return source;
    }

    const int x = qMax(0, (scaled.width() - pixelTargetSize.width()) / 2);
    const int y = qMax(0, (scaled.height() - pixelTargetSize.height()) / 2);
    return scaled.copy(x, y,
                       qMin(scaled.width(), pixelTargetSize.width()),
                       qMin(scaled.height(), pixelTargetSize.height()));
}

QDateTime itemTimestampForFile(const QFileInfo &info) {
    bool ok = false;
    const qint64 epochMs = info.completeBaseName().toLongLong(&ok);
    if (ok && epochMs > 0) {
        const QDateTime parsed = QDateTime::fromMSecsSinceEpoch(epochMs);
        if (parsed.isValid()) {
            return parsed;
        }
    }
    return info.lastModified();
}

bool isExpiredForCutoff(const QFileInfo &info, const QDateTime &cutoff) {
    return cutoff.isValid() && itemTimestampForFile(info) < cutoff;
}

QImage buildLinkPreviewImage(const QString &urlString, const QString &title, qreal targetDpr, int itemScale) {
    const QSize logicalSize = previewLogicalSize(itemScale);
    const QSize pixelSize = logicalSize * targetDpr;
    if (!pixelSize.isValid()) {
        return {};
    }

    QImage canvas(pixelSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    canvas.setDevicePixelRatio(targetDpr);

    const QUrl url(urlString);
    QString hostLabel = url.host().trimmed();
    if (hostLabel.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) {
        hostLabel.remove(0, 4);
    }
    if (hostLabel.isEmpty()) {
        hostLabel = url.toString(QUrl::RemoveScheme | QUrl::RemoveUserInfo | QUrl::RemoveFragment).trimmed();
        if (hostLabel.isEmpty()) {
            hostLabel = QStringLiteral("link");
        }
    }

    // Derive monogram from host or title.
    QString monogram;
    auto extractLetters = [](const QString &text) {
        QString token;
        const auto parts = text.split(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (!part.isEmpty()) {
                token.append(part.front().toUpper());
            }
            if (token.size() >= 2) {
                break;
            }
        }
        return token;
    };
    monogram = extractLetters(hostLabel);
    if (monogram.isEmpty()) {
        monogram = extractLetters(title);
    }
    if (monogram.isEmpty()) {
        monogram = QStringLiteral("L");
    }
    monogram = monogram.left(2);

    const QString headline = title.trimmed().isEmpty() ? hostLabel : title.trimmed();

    // Palette from host+title hash.
    const uint paletteHash = qHash(hostLabel + title);
    const int hueA = static_cast<int>(paletteHash % 360);
    const int hueB = static_cast<int>((hueA + 28 + (paletteHash % 71)) % 360);
    const QColor colorA = QColor::fromHsl(hueA, 130, 152);
    const QColor colorB = QColor::fromHsl(hueB, 122, 170);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds(QPointF(0, 0), QSizeF(logicalSize));
    QLinearGradient bg(bounds.topLeft(), bounds.bottomRight());
    bg.setColorAt(0.0, colorA.lighter(120));
    bg.setColorAt(0.48, colorB);
    bg.setColorAt(1.0, colorA.darker(116));
    painter.fillRect(bounds, bg);

    // Browser chrome
    const QRectF browserRect = bounds.adjusted(14.0, 12.0, -14.0, -12.0);
    QPainterPath browserPath;
    browserPath.addRoundedRect(browserRect, 18.0, 18.0);
    painter.fillPath(browserPath, QColor(255, 255, 255, 54));

    // Badge circle with monogram
    const QRectF heroRect(browserRect.left() + 18.0, browserRect.top() + 46.0,
                          browserRect.width() - 36.0, browserRect.height() - 80.0);
    const qreal badgeSize = qMin(heroRect.width(), heroRect.height()) * 0.38;
    const QRectF badgeRect(heroRect.center().x() - badgeSize / 2.0,
                           heroRect.top() + heroRect.height() * 0.10,
                           badgeSize, badgeSize);
    painter.setBrush(QColor(255, 255, 255, 214));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(badgeRect);

    QFont monoFont;
    monoFont.setBold(true);
    monoFont.setPointSizeF(qMax(18.0, badgeSize * 0.26));
    painter.setFont(monoFont);
    painter.setPen(QColor(58, 72, 86));
    painter.drawText(badgeRect, Qt::AlignCenter, monogram);

    // Headline text
    const QRectF headlineRect(heroRect.left(), badgeRect.bottom() + 16.0, heroRect.width(), 34.0);
    QFont headlineFont;
    headlineFont.setBold(true);
    headlineFont.setPointSizeF(qMax(11.0, bounds.height() * 0.074));
    painter.setFont(headlineFont);
    painter.setPen(QColor(255, 255, 255, 235));
    const QFontMetricsF hm(headlineFont);
    painter.drawText(headlineRect, Qt::AlignCenter,
                     hm.elidedText(headline, Qt::ElideRight, headlineRect.width()));

    painter.end();
    return canvas;
}

QImage buildTextPreviewImage(const QString &text, qreal targetDpr, int itemScale) {
    if (text.trimmed().isEmpty()) {
        return {};
    }

    const QSize logicalSize = previewLogicalSize(itemScale);
    const QSize pixelSize = logicalSize * targetDpr;
    if (!pixelSize.isValid()) {
        return {};
    }

    QImage canvas(pixelSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::white);
    canvas.setDevicePixelRatio(targetDpr);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font;
    font.setPointSize(qMax(9, 10 * itemScale / 100));
    painter.setFont(font);
    painter.setPen(QColor(40, 40, 40));

    const int pad = qMax(6, 8 * itemScale / 100);
    const QRect textRect(pad, pad, logicalSize.width() - pad * 2, logicalSize.height() - pad * 2);
    // Only use the first ~500 chars — card can't show more anyway.
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text.left(500));
    painter.end();
    return canvas;
}

QImage buildFileListPreviewImage(const QList<QUrl> &urls, qreal targetDpr, int itemScale) {
    if (urls.isEmpty()) {
        return {};
    }

    const QSize logicalSize = previewLogicalSize(itemScale);
    const QSize pixelSize = logicalSize * targetDpr;
    if (!pixelSize.isValid()) {
        return {};
    }

    QImage canvas(pixelSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::white);
    canvas.setDevicePixelRatio(targetDpr);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font;
    font.setPointSize(qMax(9, 10 * itemScale / 100));
    painter.setFont(font);
    painter.setPen(QColor(40, 40, 40));

    const int pad = qMax(8, 10 * itemScale / 100);
    QStringList names;
    for (const QUrl &url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        const QFileInfo info(path);
        names << (info.fileName().isEmpty() ? path : info.fileName());
        if (names.size() >= 20) {
            break;
        }
    }

    const QRect textRect(pad, pad, logicalSize.width() - pad * 2, logicalSize.height() - pad * 2);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, names.join(QLatin1Char('\n')));
    painter.end();
    return canvas;
}

QPixmap buildCardThumbnail(const ClipboardItem &item) {
    if (item.hasThumbnail()) {
        return item.thumbnail();
    }

    const QPixmap fullImage = item.getImage();
    if (fullImage.isNull()) {
        return QPixmap();
    }

    const QSize logicalSize = previewLogicalSize(MPasteSettings::getInst()->getItemScale());
    const qreal thumbnailDpr = maxScreenDevicePixelRatio();
    const QSize pixelTargetSize = logicalSize * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return fullImage;
    }

    QPixmap scaled = fullImage.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return QPixmap();
    }

    const int x = qMax(0, (scaled.width() - pixelTargetSize.width()) / 2);
    const int y = qMax(0, (scaled.height() - pixelTargetSize.height()) / 2);
    QPixmap thumbnail = scaled.copy(x, y,
                                    qMin(scaled.width(), pixelTargetSize.width()),
                                    qMin(scaled.height(), pixelTargetSize.height()));
    thumbnail.setDevicePixelRatio(thumbnailDpr);
    return thumbnail;
}

QPixmap buildRichTextThumbnail(const ClipboardItem &item) {
    const QMimeData *mimeData = item.getMimeData();
    if (!mimeData || !mimeData->hasHtml()) {
        return QPixmap();
    }

    const QString html = mimeData->html();
    if (html.isEmpty()) {
        return QPixmap();
    }

    const QSize logicalSize = previewLogicalSize(MPasteSettings::getInst()->getItemScale());
    const qreal thumbnailDpr = maxScreenDevicePixelRatio();
    const QSize pixelTargetSize = logicalSize * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return QPixmap();
    }

    const qreal previewZoom = htmlPreviewZoom(thumbnailDpr);
    const int leftPadding = 0;
    const int rightPadding = 0;
    const int topPadding = 0;
    const int bottomPadding = 0;
    const QSize contentSize(
        qMax(1, pixelTargetSize.width() - leftPadding - rightPadding),
        qMax(1, pixelTargetSize.height() - topPadding - bottomPadding));
    const QSizeF layoutSize(
        qMax(1.0, contentSize.width() / previewZoom),
        qMax(1.0, contentSize.height() / previewZoom));

    const QString imageSource = ContentClassifier::firstHtmlImageSource(html);
    const QByteArray imageBytes = item.imagePayloadBytesFast();
    QTextDocument document;
    configureRichTextThumbnailDocument(document, html, imageSource, imageBytes);
    document.setPageSize(layoutSize);
    document.setTextWidth(layoutSize.width());

    QPixmap snapshot(pixelTargetSize);
    snapshot.fill(Qt::transparent);

    QPainter painter(&snapshot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.translate(leftPadding, topPadding);
    painter.scale(previewZoom, previewZoom);
    painter.setClipRect(QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    document.drawContents(&painter, QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    painter.end();

    if (!richTextHtmlHasImageContent(html)) {
        snapshot.setDevicePixelRatio(thumbnailDpr);
        return snapshot;
    }

    QImage trimmed = trimTransparentPadding(snapshot.toImage(), qRound(2 * thumbnailDpr));
    QImage scaled = scaleCropToTarget(trimmed, pixelTargetSize);
    QPixmap finalPixmap = QPixmap::fromImage(scaled.isNull() ? snapshot.toImage() : scaled);
    finalPixmap.setDevicePixelRatio(thumbnailDpr);
    return finalPixmap;
}

QImage buildCardThumbnailImageFromBytes(const QByteArray &imageBytes, qreal targetDpr, int itemScale) {
    if (imageBytes.isEmpty()) {
        return QImage();
    }

    const QSize logicalSize = previewLogicalSize(itemScale);
    const QSize pixelTargetSize = logicalSize * targetDpr;
    if (!pixelTargetSize.isValid()) {
        return QImage();
    }

    QBuffer buffer;
    buffer.setData(imageBytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return QImage();
    }

    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);

    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        const QSize scaledSize = sourceSize.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding);
        if (scaledSize.isValid()) {
            reader.setScaledSize(scaledSize);
        }
    }

    QImage decoded = reader.read();
    if (decoded.isNull()) {
        decoded.loadFromData(imageBytes);
    }
    if (decoded.isNull()) {
        decoded = ContentClassifier::decodeQtSerializedImage(imageBytes);
    }
    if (decoded.isNull()) {
        return QImage();
    }

    if (decoded.size() != pixelTargetSize) {
        decoded = decoded.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    if (decoded.isNull()) {
        return QImage();
    }

    const int x = qMax(0, (decoded.width() - pixelTargetSize.width()) / 2);
    const int y = qMax(0, (decoded.height() - pixelTargetSize.height()) / 2);
    QImage thumbnail = decoded.copy(x, y,
                                    qMin(decoded.width(), pixelTargetSize.width()),
                                    qMin(decoded.height(), pixelTargetSize.height()));
    thumbnail.setDevicePixelRatio(targetDpr);
    return thumbnail;
}

QImage buildRichTextThumbnailImageFromHtml(const QString &html, const QByteArray &imageBytes, qreal thumbnailDpr, int itemScale) {
    if (html.isEmpty()) {
        return QImage();
    }

    const QSize logicalSize = previewLogicalSize(itemScale);
    const QSize pixelTargetSize = logicalSize * thumbnailDpr;
    if (!pixelTargetSize.isValid()) {
        return QImage();
    }

    const qreal previewZoom = htmlPreviewZoom(thumbnailDpr);
    const int leftPadding = 0;
    const int rightPadding = 0;
    const int topPadding = 0;
    const int bottomPadding = 0;
    const QSize contentSize(
        qMax(1, pixelTargetSize.width() - leftPadding - rightPadding),
        qMax(1, pixelTargetSize.height() - topPadding - bottomPadding));
    const QSizeF layoutSize(
        qMax(1.0, contentSize.width() / previewZoom),
        qMax(1.0, contentSize.height() / previewZoom));

    const QString imageSource = ContentClassifier::firstHtmlImageSource(html);
    QTextDocument document;
    configureRichTextThumbnailDocument(document, html, imageSource, imageBytes);
    document.setPageSize(layoutSize);
    document.setTextWidth(layoutSize.width());

    QImage snapshot(pixelTargetSize, QImage::Format_ARGB32_Premultiplied);
    snapshot.fill(Qt::transparent);

    QPainter painter(&snapshot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.translate(leftPadding, topPadding);
    painter.scale(previewZoom, previewZoom);
    painter.setClipRect(QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    document.drawContents(&painter, QRectF(0, 0, layoutSize.width(), layoutSize.height()));
    painter.end();

    if (!richTextHtmlHasImageContent(html)) {
        snapshot.setDevicePixelRatio(thumbnailDpr);
        return snapshot;
    }

    QImage trimmed = trimTransparentPadding(snapshot, qRound(2 * thumbnailDpr));
    QImage scaled = scaleCropToTarget(trimmed, pixelTargetSize);
    if (!scaled.isNull()) {
        scaled.setDevicePixelRatio(thumbnailDpr);
        return scaled;
    }
    snapshot.setDevicePixelRatio(thumbnailDpr);
    return snapshot;
}

ClipboardItem prepareItemForDisplayAndSave(const ClipboardItem &source) {
    ClipboardItem item(source);
    if (item.getContentType() == ClipboardItem::RichText
        && item.getPreviewKind() == ClipboardItem::TextPreview
        && item.hasThumbnail()) {
        item.setThumbnail(QPixmap());
        item.setThumbnailAvailableHint(false);
    }
    if (!item.hasThumbnail() && (item.getContentType() == ClipboardItem::Image
                                 || item.getContentType() == ClipboardItem::Office)) {
        const QPixmap thumbnail = buildCardThumbnail(item);
        if (!thumbnail.isNull()) {
            item.setThumbnail(thumbnail);
        }
    } else if (!item.hasThumbnail()
               && item.getContentType() == ClipboardItem::RichText
               && item.getPreviewKind() == ClipboardItem::VisualPreview) {
        const QPixmap thumbnail = buildRichTextThumbnail(item);
        if (!thumbnail.isNull()) {
            item.setThumbnail(thumbnail);
        }
    }
    return item;
}

struct PendingItemProcessingResult {
    QImage thumbnailImage;
};

} // namespace

ClipboardBoardService::ClipboardBoardService(const QString &category, QObject *parent)
    : QObject(parent),
      category_(category),
      saver_(std::make_unique<LocalSaver>()),
      thumbnailTaskPool_(std::make_unique<QThreadPool>()) {
    deferredLoadTimer_ = new QTimer(this);
    deferredLoadTimer_->setSingleShot(true);
    connect(deferredLoadTimer_, &QTimer::timeout, this, &ClipboardBoardService::continueDeferredLoad);

    const int idealThreadCount = qMax(1, QThread::idealThreadCount());
    thumbnailTaskPool_->setMaxThreadCount(qBound(1, idealThreadCount / 2, 4));
    thumbnailTaskPool_->setExpiryTimeout(15000);

    deferredSaveTimer_ = new QTimer(this);
    deferredSaveTimer_->setSingleShot(true);
    deferredSaveTimer_->setInterval(500);
    connect(deferredSaveTimer_, &QTimer::timeout, this, [this]() {
        const QList<ClipboardItem> batch = std::move(pendingSaveQueue_);
        pendingSaveQueue_.clear();
        for (const ClipboardItem &item : batch) {
            saveItemQuiet(item);
        }
    });
}

ClipboardBoardService::~ClipboardBoardService() {
    // Flush any pending deferred saves before destruction.
    if (deferredSaveTimer_) {
        deferredSaveTimer_->stop();
    }
    for (const ClipboardItem &item : pendingSaveQueue_) {
        saveItemQuiet(item);
    }
    pendingSaveQueue_.clear();

    deferredLoadActive_ = false;
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    if (indexRefreshThread_) {
        indexRefreshThread_->wait();
    }
    if (deferredLoadThread_) {
        deferredLoadThread_->wait();
    }
    if (keywordSearchThread_) {
        keywordSearchThread_->wait();
    }
    for (QThread *thread : processingThreads_) {
        if (thread) {
            thread->wait();
        }
    }
    if (thumbnailTaskPool_) {
        thumbnailTaskPool_->waitForDone();
    }
}

QThread *ClipboardBoardService::startTrackedThread(const std::function<void()> &task) {
    QThread *thread = QThread::create([task]() {
        task();
    });

    processingThreads_.append(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        processingThreads_.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return thread;
}

void ClipboardBoardService::startThumbnailTask(const std::function<void()> &task) {
    if (!thumbnailTaskPool_) {
        task();
        return;
    }

    thumbnailTaskPool_->start(QRunnable::create([task]() {
        task();
    }));
}

void ClipboardBoardService::trackExclusiveThread(QThread *thread, QThread **slot) {
    if (!thread || !slot) {
        return;
    }

    *slot = thread;
    connect(thread, &QThread::finished, this, [slot, thread]() {
        if (*slot == thread) {
            *slot = nullptr;
        }
    });
}

QString ClipboardBoardService::category() const {
    return category_;
}

QString ClipboardBoardService::saveDir() const {
    return QDir::cleanPath(MPasteSettings::getInst()->getSaveDir() + QDir::separator() + category_);
}

int ClipboardBoardService::totalItemCount() const {
    return totalItemCount_;
}

int ClipboardBoardService::pendingCount() const {
    return pendingLoadFilePaths_.size();
}

bool ClipboardBoardService::hasPendingItems() const {
    return !pendingLoadFilePaths_.isEmpty();
}

bool ClipboardBoardService::deferredLoadActive() const {
    return deferredLoadActive_;
}

void ClipboardBoardService::applyPendingFileIndex(const QStringList &filePaths,
                                                  int initialBatchSize,
                                                  int deferredBatchSize,
                                                  quint64 token) {
    if (token != asyncLoadToken_) {
        return;
    }

    Q_UNUSED(filePaths);
    Q_UNUSED(initialBatchSize);
    Q_UNUSED(deferredBatchSize);

    emit deferredLoadCompleted();
}

void ClipboardBoardService::refreshIndex() {
    deferredLoadActive_ = false;
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    waitForDeferredRead();
    waitForIndexRefresh();
    deferredLoadedItems_.clear();
    indexedItems_.clear();
    indexedFilePaths_.clear();
    pendingLoadFilePaths_.clear();
    failedFullLoadPaths_.clear();
    updateTotalItemCount(0);
    checkSaveDir();
    emit pendingCountChanged(0);
}

void ClipboardBoardService::startAsyncLoad(int initialBatchSize, int deferredBatchSize) {
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    deferredLoadActive_ = false;
    waitForDeferredRead();
    deferredLoadedItems_.clear();
    indexedItems_.clear();
    indexedFilePaths_.clear();
    pendingLoadFilePaths_.clear();
    failedFullLoadPaths_.clear();
    updateTotalItemCount(0);
    emit pendingCountChanged(0);
    checkSaveDir();

    const quint64 token = ++asyncLoadToken_;
    const QString directory = saveDir();
    QPointer<ClipboardBoardService> guard(this);
    QThread *thread = startTrackedThread([guard, directory, initialBatchSize, deferredBatchSize, token]() {
        QList<ClipboardBoardService::IndexedItemMeta> indexedItems;
        QStringList filePaths;
        LocalSaver saver;
        QDir dir(directory);
        const QFileInfoList fileInfos = dir.entryInfoList(QStringList() << "*.mpaste", QDir::Files, QDir::Name | QDir::Reversed);
        indexedItems.reserve(fileInfos.size());
        filePaths.reserve(fileInfos.size());
        for (const QFileInfo &info : fileInfos) {
            if (LocalSaver::isCurrentFormatFile(info.filePath())) {
                const QString filePath = info.filePath();
                ClipboardItem item = saver.loadFromFileLight(filePath);
                if (item.getName().isEmpty()) {
                    qWarning().noquote() << QStringLiteral("[board-service] skip unreadable history file during index build path=%1")
                        .arg(filePath);
                    continue;
                }
                filePaths.append(filePath);
                indexedItems.append(buildIndexedItemMeta(filePath, item));
            }
        }

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, indexedItems, filePaths, initialBatchSize, deferredBatchSize, token]() {
                if (guard) {
                    if (token != guard->asyncLoadToken_) {
                        return;
                    }
                    // Preserve items added via saveItemQuiet() during the
                    // async scan window — the scan started before those
                    // files were written, so they are missing from the
                    // scan result.
                    QList<IndexedItemMeta> locallyAdded;
                    QStringList locallyAddedPaths;
                    for (int i = 0; i < guard->indexedItems_.size(); ++i) {
                        const QString &path = (i < guard->indexedFilePaths_.size())
                            ? guard->indexedFilePaths_.at(i)
                            : guard->indexedItems_.at(i).filePath;
                        if (!filePaths.contains(path)) {
                            locallyAdded.append(guard->indexedItems_.at(i));
                            locallyAddedPaths.append(path);
                        }
                    }

                    guard->indexedItems_ = indexedItems;
                    guard->indexedFilePaths_ = filePaths;
                    guard->pendingLoadFilePaths_ = filePaths;

                    // Re-insert locally added items at the front.
                    for (int i = locallyAdded.size() - 1; i >= 0; --i) {
                        guard->indexedItems_.prepend(locallyAdded.at(i));
                        guard->indexedFilePaths_.prepend(locallyAddedPaths.at(i));
                    }
                    guard->updateTotalItemCount(guard->indexedItems_.size());
                    emit guard->pendingCountChanged(guard->pendingLoadFilePaths_.size());
                    if (guard->pendingLoadFilePaths_.isEmpty()) {
                        guard->deferredLoadActive_ = false;
                        emit guard->deferredLoadCompleted();
                        return;
                    }
                    guard->deferredLoadActive_ = deferredBatchSize > 0;
                    guard->deferredBatchSize_ = qMax(1, deferredBatchSize);
                    guard->loadNextBatch(initialBatchSize > 0 ? initialBatchSize
                                                              : (guard->deferredBatchSize_ > 0 ? guard->deferredBatchSize_ : 1));
                    if (guard->deferredLoadActive_ && !guard->pendingLoadFilePaths_.isEmpty() && guard->deferredLoadTimer_) {
                        guard->deferredLoadTimer_->start(guard->visibleHint_ ? 0 : 8);
                    } else if (guard->pendingLoadFilePaths_.isEmpty()) {
                        guard->deferredLoadActive_ = false;
                        emit guard->deferredLoadCompleted();
                    }
                }
            }, Qt::QueuedConnection);
        }
    });
    trackExclusiveThread(thread, &indexRefreshThread_);
}

void ClipboardBoardService::loadNextBatch(int batchSize) {
    if (batchSize <= 0 || pendingLoadFilePaths_.isEmpty()) {
        return;
    }

    QList<QPair<QString, ClipboardItem>> loadedItems;
    const int count = qMin(batchSize, pendingLoadFilePaths_.size());
    for (int i = 0; i < count; ++i) {
        const QString filePath = pendingLoadFilePaths_.takeFirst();
        ClipboardItem item = saver_->loadFromFileLight(filePath);
        if (item.getName().isEmpty()) {
            qWarning().noquote() << QStringLiteral("[board-service] skip unreadable history file during loadNextBatch path=%1")
                .arg(filePath);
            continue;
        }
        loadedItems.append(qMakePair(filePath, item));
    }

    emit pendingCountChanged(pendingLoadFilePaths_.size());
    if (!loadedItems.isEmpty()) {
        emit itemsLoaded(loadedItems);
    }
}

void ClipboardBoardService::ensureAllItemsLoaded(int batchSize) {
    deferredLoadActive_ = false;
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    waitForDeferredRead();
    while (!deferredLoadedItems_.isEmpty()) {
        processDeferredLoadedItems();
    }
    while (!pendingLoadFilePaths_.isEmpty()) {
        loadNextBatch(batchSize);
    }
}

void ClipboardBoardService::startDeferredLoad(int batchSize) {
    deferredLoadActive_ = true;
    deferredBatchSize_ = qMax(1, batchSize);
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
    scheduleDeferredLoadBatch();
}

void ClipboardBoardService::stopDeferredLoad() {
    deferredLoadActive_ = false;
    if (deferredLoadTimer_) {
        deferredLoadTimer_->stop();
    }
}

void ClipboardBoardService::setVisibleHint(bool visible) {
    visibleHint_ = visible;
}

ClipboardItem ClipboardBoardService::prepareItemForSave(const ClipboardItem &source) const {
    return prepareItemForDisplayAndSave(source);
}

void ClipboardBoardService::saveItem(const ClipboardItem &item) {
    const bool isNew = saveItemInternal(item);
    if (isNew) {
        updateTotalItemCount(indexedItems_.size());
    }
    emit localPersistenceChanged();
}

void ClipboardBoardService::saveItemQuiet(const ClipboardItem &item) {
    saveItemInternal(item);
    // No signals — caller is responsible for keeping UI in sync.
}

void ClipboardBoardService::scheduleDeferredSave(const ClipboardItem &item) {
    // Replace any pending save for the same item, keep only the latest.
    for (int i = 0; i < pendingSaveQueue_.size(); ++i) {
        if (pendingSaveQueue_[i].getName() == item.getName()) {
            pendingSaveQueue_[i] = item;
            deferredSaveTimer_->start();
            return;
        }
    }
    pendingSaveQueue_.append(item);
    deferredSaveTimer_->start();
}

bool ClipboardBoardService::hasRecentInternalWrite() const {
    return (QDateTime::currentMSecsSinceEpoch() - lastInternalWriteMs_) < 2000;
}

bool ClipboardBoardService::saveItemInternal(const ClipboardItem &item) {
    lastInternalWriteMs_ = QDateTime::currentMSecsSinceEpoch();
    checkSaveDir();
    const QString filePath = filePathForItem(item);
    const bool knownPath = indexedFilePaths_.contains(filePath);
    saver_->saveToFile(item, filePath);
    ClipboardItem lightItem = saver_->loadFromFileLight(filePath);
    if (!lightItem.getName().isEmpty()) {
        const IndexedItemMeta meta = buildIndexedItemMeta(filePath, lightItem);
        const int existingIndex = indexedFilePaths_.indexOf(filePath);
        if (existingIndex >= 0 && existingIndex < indexedItems_.size()) {
            indexedItems_[existingIndex] = meta;
        } else if (!filePath.isEmpty()) {
            indexedFilePaths_.prepend(filePath);
            indexedItems_.prepend(meta);
        }
    }
    return !knownPath && !filePath.isEmpty();
}

void ClipboardBoardService::removeItemFile(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }
    saver_->removeItem(filePath);
    const int index = indexedFilePaths_.indexOf(filePath);
    if (index >= 0) {
        indexedFilePaths_.removeAt(index);
        if (index < indexedItems_.size()) {
            indexedItems_.removeAt(index);
        }
    }
    pendingLoadFilePaths_.removeAll(filePath);
    emit localPersistenceChanged();
}

void ClipboardBoardService::deleteItemByPath(const QString &filePath) {
    deleteItemByPathInternal(filePath);
    emit localPersistenceChanged();
}

void ClipboardBoardService::deleteItemByPathQuiet(const QString &filePath) {
    deleteItemByPathInternal(filePath);
}

void ClipboardBoardService::deleteItemByPathInternal(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    lastInternalWriteMs_ = QDateTime::currentMSecsSinceEpoch();
    saver_->removeItem(filePath);
    const int index = indexedFilePaths_.indexOf(filePath);
    if (index >= 0) {
        indexedFilePaths_.removeAt(index);
        if (index < indexedItems_.size()) {
            indexedItems_.removeAt(index);
        }
    }
    const int pendingIndex = pendingLoadFilePaths_.indexOf(filePath);
    if (pendingIndex >= 0) {
        pendingLoadFilePaths_.removeAt(pendingIndex);
    }
    totalItemCount_ = qMax(0, totalItemCount_ - 1);
}

bool ClipboardBoardService::deletePendingItemByPath(const QString &filePath) {
    if (filePath.isEmpty()) {
        return false;
    }

    const int pendingIndex = pendingLoadFilePaths_.indexOf(filePath);
    if (pendingIndex < 0) {
        return false;
    }

    const int index = indexedFilePaths_.indexOf(filePath);
    if (index >= 0) {
        indexedFilePaths_.removeAt(index);
        if (index < indexedItems_.size()) {
            indexedItems_.removeAt(index);
        }
    }
    pendingLoadFilePaths_.removeAt(pendingIndex);
    saver_->removeItem(filePath);
    emit localPersistenceChanged();
    emit pendingCountChanged(pendingLoadFilePaths_.size());
    decrementTotalItemCount();
    return true;
}

QString ClipboardBoardService::filePathForItem(const ClipboardItem &item) const {
    return filePathForName(item.getName());
}

QString ClipboardBoardService::filePathForName(const QString &name) const {
    if (name.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(saveDir() + QDir::separator() + name + ".mpaste");
}

ClipboardItem ClipboardBoardService::loadItemLight(const QString &filePath, bool includeThumbnail) {
    return saver_->loadFromFileLight(filePath, includeThumbnail);
}

void ClipboardBoardService::refreshIndexedItemForPath(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    const int existingIndex = indexedFilePaths_.indexOf(filePath);
    if (!QFileInfo::exists(filePath) || !LocalSaver::isCurrentFormatFile(filePath)) {
        if (existingIndex >= 0) {
            indexedFilePaths_.removeAt(existingIndex);
            if (existingIndex < indexedItems_.size()) {
                indexedItems_.removeAt(existingIndex);
            }
            updateTotalItemCount(indexedItems_.size());
        }
        emit localPersistenceChanged();
        return;
    }

    ClipboardItem lightItem = saver_->loadFromFileLight(filePath);
    if (lightItem.getName().isEmpty()) {
        if (existingIndex >= 0) {
            indexedFilePaths_.removeAt(existingIndex);
            if (existingIndex < indexedItems_.size()) {
                indexedItems_.removeAt(existingIndex);
            }
            updateTotalItemCount(indexedItems_.size());
        }
        emit localPersistenceChanged();
        return;
    }

    const IndexedItemMeta meta = buildIndexedItemMeta(filePath, lightItem);
    if (existingIndex >= 0 && existingIndex < indexedItems_.size()) {
        indexedItems_[existingIndex] = meta;
    } else {
        indexedFilePaths_.prepend(filePath);
        indexedItems_.prepend(meta);
        updateTotalItemCount(indexedItems_.size());
    }
    emit localPersistenceChanged();
}

int ClipboardBoardService::filteredItemCount(ClipboardItem::ContentType type,
                                             const QString &keyword,
                                             const QSet<QString> &matchedNames) const {
    if (indexedItems_.isEmpty()) {
        return 0;
    }

    int count = 0;
    for (const auto &entry : indexedItems_) {
        if (indexedItemMatchesFilter(entry, type, keyword, matchedNames)) {
            ++count;
        }
    }
    return count;
}

QList<QPair<QString, ClipboardItem>> ClipboardBoardService::loadIndexedSlice(int offset, int count, bool includeThumbnail) {
    QList<QPair<QString, ClipboardItem>> loadedItems;
    if (count <= 0 || offset < 0 || offset >= indexedItems_.size()) {
        return loadedItems;
    }

    const int end = qMin(indexedItems_.size(), offset + count);
    loadedItems.reserve(end - offset);
    for (int i = offset; i < end; ++i) {
        const QString &filePath = indexedItems_.at(i).filePath;
        ClipboardItem item = saver_->loadFromFileLight(filePath, includeThumbnail);
        if (item.getName().isEmpty()) {
            continue;
        }
        loadedItems.append(qMakePair(filePath, item));
    }
    return loadedItems;
}

QList<QPair<QString, ClipboardItem>> ClipboardBoardService::loadFilteredIndexedSlice(ClipboardItem::ContentType type,
                                                                                      const QString &keyword,
                                                                                      const QSet<QString> &matchedNames,
                                                                                      int offset,
                                                                                      int count,
                                                                                      bool includeThumbnail) {
    QList<QPair<QString, ClipboardItem>> loadedItems;
    if (count <= 0 || offset < 0 || indexedItems_.isEmpty()) {
        return loadedItems;
    }

    int matchedIndex = 0;
    for (int i = 0; i < indexedItems_.size(); ++i) {
        const auto &entry = indexedItems_.at(i);
        if (!indexedItemMatchesFilter(entry, type, keyword, matchedNames)) {
            continue;
        }
        if (matchedIndex++ < offset) {
            continue;
        }
        const QString &filePath = entry.filePath;
        ClipboardItem item = saver_->loadFromFileLight(filePath, includeThumbnail);
        if (item.getName().isEmpty()) {
            continue;
        }
        loadedItems.append(qMakePair(filePath, item));
        if (loadedItems.size() >= count) {
            break;
        }
    }
    return loadedItems;
}

QSet<QByteArray> ClipboardBoardService::loadAllFingerprints() {
    QSet<QByteArray> fingerprints;
    if (!indexedItems_.isEmpty()) {
        for (const auto &entry : indexedItems_) {
            if (!entry.name.isEmpty()) {
                fingerprints.insert(entry.fingerprint);
            }
        }
        return fingerprints;
    }

    checkSaveDir();
    QDir dir(saveDir());
    const QFileInfoList fileInfos = dir.entryInfoList(QStringList() << "*.mpaste", QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &info : fileInfos) {
        if (!LocalSaver::isCurrentFormatFile(info.filePath())) {
            continue;
        }
        const ClipboardItem item = saver_->loadFromFileLight(info.filePath());
        if (!item.getName().isEmpty()) {
            fingerprints.insert(item.fingerprint());
        }
    }
    return fingerprints;
}

bool ClipboardBoardService::containsFingerprint(const QByteArray &fingerprint) const {
    if (fingerprint.isEmpty()) {
        return false;
    }
    for (const auto &entry : indexedItems_) {
        if (entry.fingerprint == fingerprint) {
            return true;
        }
    }
    return false;
}

void ClipboardBoardService::notifyItemAdded() {
    updateTotalItemCount(totalItemCount_ + 1);
}

bool ClipboardBoardService::moveIndexedItemToFront(const QString &name) {
    const QString filePath = filePathForName(name);
    if (filePath.isEmpty()) {
        return false;
    }

    const int index = indexedFilePaths_.indexOf(filePath);
    if (index <= 0) {
        return index == 0;
    }

    indexedFilePaths_.move(index, 0);
    if (index < indexedItems_.size()) {
        indexedItems_.move(index, 0);
    }
    return true;
}

void ClipboardBoardService::trimExpiredPendingItems(const QDateTime &cutoff) {
    Q_UNUSED(cutoff);
    if (category_ == MPasteSettings::STAR_CATEGORY_NAME) {
        return;
    }
}

int ClipboardBoardService::maintainPreviewCache(PreviewCacheMaintenanceMode mode) {
    checkSaveDir();

    QDir dir(saveDir());
    const QFileInfoList fileInfos = dir.entryInfoList(QStringList() << "*.mpaste", QDir::Files, QDir::Name);
    if (fileInfos.isEmpty()) {
        return 0;
    }

    LocalSaver saver;
    int changedCount = 0;

    for (const QFileInfo &info : fileInfos) {
        const QString filePath = info.filePath();
        if (!LocalSaver::isCurrentFormatFile(filePath)) {
            continue;
        }

        const ClipboardItem lightItem = saver.loadFromFileLight(filePath);
        if (lightItem.getName().isEmpty() || !isPreviewCacheManagedContent(lightItem)) {
            continue;
        }

        switch (mode) {
            case ClearAllPreviews: {
                const ClipboardItem fullItem = saver.loadFromFile(filePath);
                if (fullItem.getName().isEmpty() || !fullItem.hasThumbnail()) {
                    break;
                }
                if (saver.updateThumbnail(filePath, QPixmap())) {
                    ++changedCount;
                }
                break;
            }
            case RepairBrokenPreviews: {
                if (lightItem.getContentType() == ClipboardItem::RichText
                    && lightItem.getPreviewKind() == ClipboardItem::TextPreview) {
                    const ClipboardItem fullItem = saver.loadFromFile(filePath);
                    if (!fullItem.getName().isEmpty()
                        && fullItem.hasThumbnail()
                        && saver.updateThumbnail(filePath, QPixmap())) {
                        ++changedCount;
                    }
                    break;
                }

                if (!usesVisualPreview(lightItem) || lightItem.hasThumbnailHint()) {
                    break;
                }

                const ClipboardItem fullItem = saver.loadFromFile(filePath);
                if (fullItem.getName().isEmpty()) {
                    break;
                }

                const ClipboardItem preparedItem = prepareItemForDisplayAndSave(fullItem);
                if (!preparedItem.thumbnail().isNull()
                    && saver.updateThumbnail(filePath, preparedItem.thumbnail())) {
                    ++changedCount;
                }
                break;
            }
            case RebuildAllPreviews: {
                const ClipboardItem fullItem = saver.loadFromFile(filePath);
                if (fullItem.getName().isEmpty()) {
                    break;
                }

                const ClipboardItem preparedItem = prepareItemForDisplayAndSave(fullItem);
                if (preparedItem.thumbnail().cacheKey() == fullItem.thumbnail().cacheKey()) {
                    break;
                }
                if (saver.updateThumbnail(filePath, preparedItem.thumbnail())) {
                    ++changedCount;
                }
                break;
            }
        }
    }

    if (changedCount > 0) {
        emit localPersistenceChanged();
    }
    return changedCount;
}

void ClipboardBoardService::processPendingItemAsync(const ClipboardItem &item, const QString &expectedName) {
    if (expectedName.isEmpty()) {
        return;
    }

    const ClipboardItem::ContentType contentType = item.getContentType();
    const ClipboardItem::PreviewKind previewKind = item.getPreviewKind();
    const ClipboardItem baseItem = item;
    const QByteArray imageBytes = (contentType == ClipboardItem::Image
            || contentType == ClipboardItem::Office
            || (contentType == ClipboardItem::RichText && previewKind == ClipboardItem::VisualPreview))
        ? item.imagePayloadBytesFast()
        : QByteArray();
    const QString richHtml = (contentType == ClipboardItem::RichText && previewKind == ClipboardItem::VisualPreview)
        ? item.getHtml()
        : QString();
    const QSize imageSize = item.isMimeDataLoaded()
        && (contentType == ClipboardItem::Image || contentType == ClipboardItem::Office)
        ? item.getImagePixelSize()
        : QSize();
    const QString sourceFilePath = item.sourceFilePath();
    const quint64 mimeOffset = item.mimeDataFileOffset();
    const qreal thumbnailDpr = maxScreenDevicePixelRatio();
    const int itemScale = MPasteSettings::getInst()->getItemScale();

    QPointer<ClipboardBoardService> guard(this);
    startThumbnailTask([guard, expectedName, contentType, previewKind, baseItem, imageBytes, richHtml, imageSize, sourceFilePath, mimeOffset, thumbnailDpr, itemScale]() mutable {
        PendingItemProcessingResult result;
        QByteArray resolvedImageBytes = imageBytes;
        QString resolvedHtml = richHtml;
        if ((resolvedImageBytes.isEmpty() || resolvedHtml.isEmpty())
            && !sourceFilePath.isEmpty()
            && (contentType == ClipboardItem::Image
                || contentType == ClipboardItem::Office
                || (contentType == ClipboardItem::RichText && previewKind == ClipboardItem::VisualPreview))) {
            QString htmlPayload;
            QByteArray imagePayload;
            LocalSaver::loadMimePayloads(sourceFilePath,
                                         mimeOffset,
                                         (contentType == ClipboardItem::RichText && previewKind == ClipboardItem::VisualPreview) ? &htmlPayload : nullptr,
                                         (contentType == ClipboardItem::Image
                                            || contentType == ClipboardItem::Office
                                            || (contentType == ClipboardItem::RichText && previewKind == ClipboardItem::VisualPreview)) ? &imagePayload : nullptr);
            if (resolvedHtml.isEmpty()) {
                resolvedHtml = htmlPayload;
            }
            if (resolvedImageBytes.isEmpty()) {
                resolvedImageBytes = imagePayload;
            }
        }

        if ((contentType == ClipboardItem::Image || contentType == ClipboardItem::Office)
            && !resolvedImageBytes.isEmpty()) {
            result.thumbnailImage = buildCardThumbnailImageFromBytes(resolvedImageBytes, thumbnailDpr, itemScale);
            if (isVeryTallImage(imageSize)) {
                qInfo().noquote() << QStringLiteral("[thumb-build] stage=worker name=%1 image=%2x%3 thumbPx=%4x%5 thumbDpr=%6")
                    .arg(expectedName)
                    .arg(imageSize.width())
                    .arg(imageSize.height())
                    .arg(result.thumbnailImage.width())
                    .arg(result.thumbnailImage.height())
                    .arg(result.thumbnailImage.devicePixelRatio(), 0, 'f', 2);
            }
        } else if (contentType == ClipboardItem::RichText
                   && previewKind == ClipboardItem::VisualPreview
                   && !resolvedHtml.isEmpty()) {
            result.thumbnailImage = buildRichTextThumbnailImageFromHtml(resolvedHtml, resolvedImageBytes, thumbnailDpr, itemScale);
        } else if (contentType == ClipboardItem::Link && !baseItem.hasThumbnail()) {
            QString linkUrl;
            const QList<QUrl> urls = baseItem.getNormalizedUrls();
            if (!urls.isEmpty()) {
                const QUrl &first = urls.first();
                linkUrl = first.isLocalFile() ? first.toLocalFile() : first.toString();
            } else {
                linkUrl = baseItem.getNormalizedText().left(512).trimmed();
            }
            result.thumbnailImage = buildLinkPreviewImage(linkUrl, baseItem.getTitle(), thumbnailDpr, itemScale);
        }

        // Save to disk in the worker thread to avoid blocking the UI.
        QString savedFilePath;
        {
            ClipboardItem saveItem = baseItem;
            if (!result.thumbnailImage.isNull()) {
                QPixmap thumbnail = QPixmap::fromImage(result.thumbnailImage);
                thumbnail.setDevicePixelRatio(qMax<qreal>(1.0, thumbnailDpr));
                saveItem.setThumbnail(thumbnail);
            }
            if (guard) {
                savedFilePath = guard->filePathForItem(saveItem);
                LocalSaver saver;
                saver.saveToFile(saveItem, savedFilePath);
            }
        }

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, expectedName, baseItem, result, thumbnailDpr, savedFilePath]() mutable {
                if (!guard) {
                    return;
                }

                ClipboardItem preparedItem = baseItem;
                if (preparedItem.getName().isEmpty()) {
                    return;
                }

                if (!result.thumbnailImage.isNull()) {
                    QPixmap thumbnail = QPixmap::fromImage(result.thumbnailImage);
                    thumbnail.setDevicePixelRatio(qMax<qreal>(1.0, thumbnailDpr));
                    preparedItem.setThumbnail(thumbnail);
                    const QSize imageSize = preparedItem.getImagePixelSize();
                    if (isVeryTallImage(imageSize)) {
                        qInfo().noquote() << QStringLiteral("[thumb-build] stage=ui name=%1 image=%2x%3 thumbPx=%4x%5 thumbLogical=%6x%7 thumbDpr=%8")
                            .arg(expectedName)
                            .arg(imageSize.width())
                            .arg(imageSize.height())
                            .arg(thumbnail.width())
                            .arg(thumbnail.height())
                            .arg(qRound(thumbnail.width() / qMax<qreal>(1.0, thumbnail.devicePixelRatio())))
                            .arg(qRound(thumbnail.height() / qMax<qreal>(1.0, thumbnail.devicePixelRatio())))
                            .arg(thumbnail.devicePixelRatio(), 0, 'f', 2);
                    }
                }

                // Update the service index from the saved file (lightweight)
                // and propagate sourceFilePath/mimeDataFileOffset so that
                // preview can read image data from disk after mimeData_ is
                // released.
                if (!savedFilePath.isEmpty()) {
                    LocalSaver indexSaver;
                    ClipboardItem lightItem = indexSaver.loadFromFileLight(savedFilePath);
                    if (!lightItem.getName().isEmpty()) {
                        const IndexedItemMeta meta = buildIndexedItemMeta(savedFilePath, lightItem);
                        const int existingIndex = guard->indexedFilePaths_.indexOf(savedFilePath);
                        if (existingIndex >= 0 && existingIndex < guard->indexedItems_.size()) {
                            guard->indexedItems_[existingIndex] = meta;
                        } else {
                            guard->indexedFilePaths_.prepend(savedFilePath);
                            guard->indexedItems_.prepend(meta);
                        }
                        preparedItem.setSourceFilePath(savedFilePath);
                        preparedItem.setMimeDataFileOffset(lightItem.mimeDataFileOffset());
                    }
                    guard->lastInternalWriteMs_ = QDateTime::currentMSecsSinceEpoch();
                }

                emit guard->pendingItemReady(expectedName, preparedItem);
            }, Qt::QueuedConnection);
        }
    });
}

void ClipboardBoardService::requestThumbnailAsync(const QString &expectedName, const QString &filePath) {
    if (expectedName.isEmpty() || filePath.isEmpty()) {
        return;
    }

    QPointer<ClipboardBoardService> guard(this);
    startThumbnailTask([guard, expectedName, filePath]() mutable {
        LocalSaver saver;
        ClipboardItem loaded = saver.loadFromFileLight(filePath);
        ClipboardItem preparedItem = loaded;
        bool generatedThumbnail = false;
        bool refreshedRichText = false;
        bool attemptedRebuild = false;
        bool rebuildFailed = false;
        bool loadedPersistedThumbnail = false;
        const QString loadedNormalizedText = loaded.getNormalizedText();
        if (!loaded.getName().isEmpty()) {
            const ClipboardItem::ContentType type = loaded.getContentType();
            if (loaded.hasThumbnailHint() && loaded.thumbnail().isNull()) {
                ClipboardItem thumbnailItem = saver.loadFromFileLight(filePath, true);
                if (!thumbnailItem.getName().isEmpty() && !thumbnailItem.thumbnail().isNull()) {
                    preparedItem.setThumbnail(thumbnailItem.thumbnail());
                    loadedPersistedThumbnail = true;
                }
            }

            const bool shouldRebuild =
                (type == ClipboardItem::RichText && loaded.getPreviewKind() == ClipboardItem::VisualPreview)
                || (preparedItem.thumbnail().isNull()
                    && (type == ClipboardItem::Image
                        || type == ClipboardItem::Office));
            if (shouldRebuild && !(guard && guard->failedFullLoadPaths_.contains(filePath))) {
                attemptedRebuild = true;
                if (type == ClipboardItem::RichText) {
                    QString htmlPayload;
                    QByteArray imagePayload;
                    if (LocalSaver::loadMimePayloads(filePath,
                                                     loaded.mimeDataFileOffset(),
                                                     &htmlPayload,
                                                     &imagePayload)
                        && !htmlPayload.isEmpty()) {
                        const qreal thumbnailDpr = maxScreenDevicePixelRatio();
                        const int itemScale = MPasteSettings::getInst()->getItemScale();
                        const QImage thumbnailImage = buildRichTextThumbnailImageFromHtml(htmlPayload,
                                                                                          imagePayload,
                                                                                          thumbnailDpr,
                                                                                          itemScale);
                        if (!thumbnailImage.isNull()) {
                            QPixmap thumbnail = QPixmap::fromImage(thumbnailImage);
                            thumbnail.setDevicePixelRatio(qMax<qreal>(1.0, thumbnailDpr));
                            preparedItem.setThumbnail(thumbnail);
                            generatedThumbnail = preparedItem.thumbnail().cacheKey() != loaded.thumbnail().cacheKey();
                            refreshedRichText = true;
                        }
                    }

                    if (!generatedThumbnail) {
                        ClipboardItem fullItem = saver.loadFromFile(filePath);
                        if (!fullItem.getName().isEmpty()) {
                            preparedItem = prepareItemForDisplayAndSave(fullItem);
                            generatedThumbnail = !preparedItem.thumbnail().isNull()
                                && preparedItem.thumbnail().cacheKey() != loaded.thumbnail().cacheKey();
                            refreshedRichText = !preparedItem.thumbnail().isNull();
                        }
                    }

                    if (!generatedThumbnail && !refreshedRichText) {
                        rebuildFailed = true;
                    }
                } else {
                    ClipboardItem fullItem = saver.loadFromFile(filePath);
                    if (!fullItem.getName().isEmpty()) {
                        preparedItem = prepareItemForDisplayAndSave(fullItem);
                        generatedThumbnail = !preparedItem.thumbnail().isNull()
                            && preparedItem.thumbnail().cacheKey() != loaded.thumbnail().cacheKey();
                        refreshedRichText = type == ClipboardItem::RichText;
                    } else {
                        rebuildFailed = true;
                    }
                }
            } else if (shouldRebuild) {
                rebuildFailed = true;
            }
        }
        const QPixmap thumbnail = preparedItem.thumbnail();
        const bool shouldPersistPreparedItem = generatedThumbnail
            || (refreshedRichText && preparedItem.getNormalizedText() != loadedNormalizedText);
        const bool noThumbnailProgress = attemptedRebuild
            && thumbnail.isNull()
            && !generatedThumbnail
            && !refreshedRichText
            && !loadedPersistedThumbnail;

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, expectedName, filePath, preparedItem, thumbnail, generatedThumbnail, refreshedRichText, shouldPersistPreparedItem, rebuildFailed, noThumbnailProgress]() {
                if (!guard) {
                    return;
                }
                if (rebuildFailed && !filePath.isEmpty()) {
                    guard->failedFullLoadPaths_.insert(filePath);
                }
                if ((generatedThumbnail || refreshedRichText) && !preparedItem.getName().isEmpty()) {
                    // Emit the thumbnail immediately so the UI updates
                    // without any disk I/O on the main thread.  Persist
                    // the file later via a deferred call so the current
                    // event-loop iteration stays responsive.
                    emit guard->thumbnailReady(expectedName, thumbnail);
                    if (shouldPersistPreparedItem) {
                        guard->saveItemQuiet(preparedItem);
                    }
                    return;
                }
                if (rebuildFailed || noThumbnailProgress) {
                    emit guard->thumbnailReady(expectedName, QPixmap());
                    return;
                }
                emit guard->thumbnailReady(expectedName, thumbnail);
            }, Qt::QueuedConnection);
        }
    });
}

void ClipboardBoardService::startAsyncKeywordSearch(const QList<QPair<QString, quint64>> &candidates,
                                                    const QString &keyword,
                                                    quint64 token) {
    if (candidates.isEmpty() || keyword.isEmpty()) {
        return;
    }

    QPointer<ClipboardBoardService> guard(this);
    QThread *thread = startTrackedThread([guard, candidates, keyword, token]() {
        QSet<QString> matchedNames;
        for (const auto &candidate : candidates) {
            if (candidate.first.isEmpty()) {
                continue;
            }
            if (LocalSaver::mimeSectionContainsKeyword(candidate.first, candidate.second, keyword)) {
                const QFileInfo info(candidate.first);
                matchedNames.insert(info.completeBaseName());
            }
        }

        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, matchedNames, token]() {
                if (!guard) {
                    return;
                }
                emit guard->keywordMatched(matchedNames, token);
            }, Qt::QueuedConnection);
        }
    });
    trackExclusiveThread(thread, &keywordSearchThread_);
}

void ClipboardBoardService::continueDeferredLoad() {
    if (!deferredLoadActive_) {
        return;
    }

    if (!pendingLoadFilePaths_.isEmpty()) {
        loadNextBatch(deferredBatchSize_);
    }

    if (pendingLoadFilePaths_.isEmpty()) {
        deferredLoadActive_ = false;
        emit deferredLoadCompleted();
    } else if (deferredLoadTimer_) {
        deferredLoadTimer_->start(visibleHint_ ? 0 : 8);
    }
}

void ClipboardBoardService::handleDeferredBatchRead(const QList<QPair<QString, QByteArray>> &batchPayloads) {
    if (!batchPayloads.isEmpty()) {
        deferredLoadedItems_.append(batchPayloads);
    }

    if (deferredLoadTimer_ && !deferredLoadTimer_->isActive()) {
        deferredLoadTimer_->start(visibleHint_ ? 0 : 8);
    }

    if (deferredLoadActive_ && !pendingLoadFilePaths_.isEmpty()) {
        scheduleDeferredLoadBatch();
    }
}

void ClipboardBoardService::checkSaveDir() {
    QDir dir;
    const QString path = QDir::cleanPath(saveDir());
    if (!dir.exists(path)) {
        dir.mkpath(path);
    }
}

void ClipboardBoardService::updateTotalItemCount(int total) {
    if (total == totalItemCount_) {
        return;
    }
    totalItemCount_ = qMax(0, total);
    emit totalItemCountChanged(totalItemCount_);
}

void ClipboardBoardService::decrementTotalItemCount() {
    if (totalItemCount_ <= 0) {
        return;
    }
    updateTotalItemCount(totalItemCount_ - 1);
}

void ClipboardBoardService::scheduleDeferredLoadBatch() {
    if (!deferredLoadActive_ || deferredLoadThread_ || pendingLoadFilePaths_.isEmpty()) {
        return;
    }

    startRawReadBatch(deferredBatchSize_);
}

void ClipboardBoardService::startRawReadBatch(int batchSize) {
    if (batchSize <= 0 || deferredLoadThread_ || pendingLoadFilePaths_.isEmpty()) {
        return;
    }

    const int count = qMin(batchSize, pendingLoadFilePaths_.size());
    QStringList batchPaths;
    batchPaths.reserve(count);
    for (int i = 0; i < count; ++i) {
        batchPaths.append(pendingLoadFilePaths_.takeFirst());
    }
    emit pendingCountChanged(pendingLoadFilePaths_.size());

    QPointer<ClipboardBoardService> guard(this);
    QThread *thread = startTrackedThread([guard, batchPaths]() {
        QList<QPair<QString, QByteArray>> batchPayloads;
        batchPayloads.reserve(batchPaths.size());
        for (const QString &filePath : batchPaths) {
            QByteArray rawData;
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly)) {
                rawData = file.readAll();
                file.close();
            }
            batchPayloads.append(qMakePair(filePath, rawData));
        }
        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, batchPayloads]() {
                if (guard) {
                    guard->handleDeferredBatchRead(batchPayloads);
                }
            }, Qt::QueuedConnection);
        }
    });
    trackExclusiveThread(thread, &deferredLoadThread_);
}

void ClipboardBoardService::processDeferredLoadedItems() {
    if (deferredLoadedItems_.isEmpty()) {
        return;
    }

    QElapsedTimer parseTimer;
    parseTimer.start();
    const bool widgetVisible = visibleHint_;
    const int maxItemsPerTick = widgetVisible ? 2 : 1;
    const int maxParseMs = widgetVisible ? 12 : 4;
    int processedCount = 0;
    QList<QPair<QString, ClipboardItem>> loadedItems;

    while (!deferredLoadedItems_.isEmpty() && processedCount < maxItemsPerTick && parseTimer.elapsed() < maxParseMs) {
        const auto payload = deferredLoadedItems_.takeFirst();
        const QString filePath = payload.first;
        const QByteArray rawData = payload.second;
        ClipboardItem item = rawData.isEmpty() ? ClipboardItem() : saver_->loadFromRawDataLight(rawData, filePath);
        if (item.getName().isEmpty()) {
            qWarning().noquote() << QStringLiteral("[board-service] preserve unreadable deferred history file path=%1 rawSize=%2")
                .arg(filePath)
                .arg(rawData.size());
        } else {
            loadedItems.append(qMakePair(filePath, item));
        }
        ++processedCount;
    }

    if (!loadedItems.isEmpty()) {
        emit itemsLoaded(loadedItems);
    }

    if (!deferredLoadedItems_.isEmpty() && deferredLoadTimer_) {
        deferredLoadTimer_->start(widgetVisible ? 0 : 8);
    }
}

void ClipboardBoardService::waitForDeferredRead() {
    if (deferredLoadThread_) {
        deferredLoadThread_->wait();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

void ClipboardBoardService::waitForIndexRefresh() {
    if (indexRefreshThread_) {
        indexRefreshThread_->wait();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}
