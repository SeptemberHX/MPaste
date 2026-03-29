// input: Depends on ThumbnailBuilder.h, CardPreviewMetrics, ContentClassifier, MPasteSettings, and Qt painting APIs.
// output: Implements thumbnail and preview image generation for clipboard items.
// pos: utils layer thumbnail builder implementation.
// update: If I change, update this header block and my folder README.md.
#include "ThumbnailBuilder.h"

#include <QBuffer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QScreen>
#include <QTextOption>
#include <QUrl>

#include "data/CardPreviewMetrics.h"
#include "data/ContentClassifier.h"
#include "utils/MPasteSettings.h"
#include "utils/MxGraphRenderer.h"
#include "utils/ThemeManager.h"

namespace ThumbnailBuilder {

QVariant OfflineTextDocument::loadResource(int type, const QUrl &url) {
    Q_UNUSED(type);
    Q_UNUSED(url);
    return QVariant();
}

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
        " padding: 12px 12px 0 12px !important;"
        " font-size: 15px !important;"
        " line-height: 1.38 !important;"
        " }"
        "body * {"
        " margin: 0 !important;"
        " max-width: 100% !important;"
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

// Keep only basic text-formatting tags, drop everything that could
// trigger resource loading or heavy layout (images, styles, SVGs,
// shadow DOM templates, scripts, etc.).
QString simplifyHtmlForRendering(const QString &html) {
    static const QSet<QString> allowed = {
        QStringLiteral("p"), QStringLiteral("br"),
        QStringLiteral("b"), QStringLiteral("strong"),
        QStringLiteral("i"), QStringLiteral("em"),
        QStringLiteral("u"), QStringLiteral("s"),
        QStringLiteral("a"), QStringLiteral("span"),
        QStringLiteral("div"), QStringLiteral("blockquote"),
        QStringLiteral("pre"), QStringLiteral("code"),
        QStringLiteral("font"), QStringLiteral("sub"), QStringLiteral("sup"),
        QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
        QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"),
        QStringLiteral("ul"), QStringLiteral("ol"), QStringLiteral("li"),
        QStringLiteral("table"), QStringLiteral("tr"),
        QStringLiteral("td"), QStringLiteral("th"),
        QStringLiteral("html"), QStringLiteral("head"), QStringLiteral("body"),
    };
    QString result;
    result.reserve(html.size());
    int i = 0;
    const int len = html.size();
    while (i < len) {
        if (html[i] == QLatin1Char('<')) {
            // Skip HTML comments (<!--...-->).
            if (i + 3 < len && html[i + 1] == QLatin1Char('!')
                && html[i + 2] == QLatin1Char('-') && html[i + 3] == QLatin1Char('-')) {
                int commentEnd = html.indexOf(QStringLiteral("-->"), i + 4);
                i = (commentEnd >= 0) ? commentEnd + 3 : len;
                continue;
            }
            // Skip <!DOCTYPE ...> declarations.
            if (i + 1 < len && html[i + 1] == QLatin1Char('!')) {
                int tagEnd = html.indexOf(QLatin1Char('>'), i);
                i = (tagEnd >= 0) ? tagEnd + 1 : len;
                continue;
            }
            int tagEnd = html.indexOf(QLatin1Char('>'), i);
            if (tagEnd < 0) break;
            int nameStart = i + 1;
            if (nameStart < tagEnd && html[nameStart] == QLatin1Char('/')) ++nameStart;
            int nameEnd = nameStart;
            while (nameEnd < tagEnd && html[nameEnd] != QLatin1Char(' ')
                   && html[nameEnd] != QLatin1Char('>') && html[nameEnd] != QLatin1Char('/')) {
                ++nameEnd;
            }
            const QString tagName = html.mid(nameStart, nameEnd - nameStart).toLower();
            if (allowed.contains(tagName)) {
                if (html[i + 1] == QLatin1Char('/')) {
                    result += QStringLiteral("</%1>").arg(tagName);
                } else {
                    // Keep opening tag with style, but strip url()
                    // references that trigger remote resource loading.
                    QString tagStr = QStringView(html).mid(i, tagEnd - i + 1).toString();
                    static const QRegularExpression urlInStyle(
                        QStringLiteral("url\\s*\\([^)]*\\)"),
                        QRegularExpression::CaseInsensitiveOption);
                    tagStr.replace(urlInStyle, QStringLiteral("none"));
                    static const QRegularExpression remoteSrc(
                        QStringLiteral("(?:src|srcset)\\s*=\\s*[\"'][^\"']*[\"']"),
                        QRegularExpression::CaseInsensitiveOption);
                    tagStr.replace(remoteSrc, QString());
                    result += tagStr;
                }
            } else if (html[i + 1] != QLatin1Char('/')) {
                // Only replace dropped *block-level* tags with <br> to
                // preserve visual line breaks.  Head-section tags (meta,
                // style, link, title, script) and inline elements are
                // silently dropped so they don't inject blank lines at
                // the top of the rendered preview.
                static const QSet<QString> headOrInline = {
                    QStringLiteral("meta"), QStringLiteral("style"),
                    QStringLiteral("link"), QStringLiteral("title"),
                    QStringLiteral("script"), QStringLiteral("noscript"),
                    QStringLiteral("img"), QStringLiteral("svg"),
                    QStringLiteral("input"), QStringLiteral("button"),
                    QStringLiteral("select"), QStringLiteral("textarea"),
                    QStringLiteral("label"), QStringLiteral("object"),
                    QStringLiteral("embed"), QStringLiteral("iframe"),
                    QStringLiteral("template"), QStringLiteral("slot"),
                };
                if (!headOrInline.contains(tagName)) {
                    result += QStringLiteral("<br>");
                }
            }
            i = tagEnd + 1;
        } else {
            result += html[i];
            ++i;
        }
    }
    return result;
}

QString richTextHtmlForThumbnail(QString html) {
    html = simplifyHtmlForRendering(html);
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

    if (!imageSource.isEmpty() && !imageBytes.isEmpty()) {
        QImage image;
        if (!image.loadFromData(imageBytes)) {
            image = ContentClassifier::decodeQtSerializedImage(imageBytes);
        }
        if (!image.isNull()) {
            document.addResource(QTextDocument::ImageResource, QUrl(imageSource), image);
        }
    }

    // Apply the thumbnail stylesheet via setDefaultStyleSheet (lower
    // priority than inline styles, so colors/formatting are preserved).
    document.setDefaultStyleSheet(richTextThumbnailStyleSheet());
    document.setHtml(simplifyHtmlForRendering(html));
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

bool richTextHtmlHasImageContent(const QString &html) {
    return !ContentClassifier::firstHtmlImageSource(html).isEmpty()
        || html.contains(QStringLiteral("<img"), Qt::CaseInsensitive);
}

bool isPreviewCacheManagedContent(const ClipboardItem &item) {
    const ContentType type = item.getContentType();
    return type == Image
        || type == Office
        || type == RichText;
}

bool usesVisualPreview(const ClipboardItem &item) {
    const ContentType type = item.getContentType();
    return type == Image
        || type == Office
        || (type == RichText && item.getPreviewKind() == VisualPreview);
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

    if (MxGraphRenderer::isMxGraphContent(html) || MxGraphRenderer::isMxGraphContent(item.getNormalizedText())) {
        const int itemScale = MPasteSettings::getInst()->getItemScale();
        const qreal dpr = maxScreenDevicePixelRatio();
        const QImage mxImage = buildMxGraphThumbnailImage(html, item.getNormalizedText(), dpr, itemScale);
        if (!mxImage.isNull()) {
            QPixmap px = QPixmap::fromImage(mxImage);
            px.setDevicePixelRatio(dpr);
            return px;
        }
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
    OfflineTextDocument document;
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

    // draw.io mxGraphModel content: render the diagram instead of the
    // invisible HTML wrapper (transparent text, 0px font).
    if (MxGraphRenderer::isMxGraphContent(html)) {
        const QImage mxImage = buildMxGraphThumbnailImage(html, QString(), thumbnailDpr, itemScale);
        if (!mxImage.isNull()) {
            return mxImage;
        }
    }

    const QString &truncatedHtml = html;

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

    const QString imageSource = ContentClassifier::firstHtmlImageSource(truncatedHtml);
    OfflineTextDocument document;
    configureRichTextThumbnailDocument(document, truncatedHtml, imageSource, imageBytes);
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

    if (!richTextHtmlHasImageContent(truncatedHtml)) {
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

bool isMxGraphRichText(const QString &html, const QString &normalizedText) {
    return MxGraphRenderer::isMxGraphContent(html)
        || MxGraphRenderer::isMxGraphContent(normalizedText);
}

QImage buildMxGraphThumbnailImage(const QString &html, const QString &normalizedText, qreal targetDpr, int itemScale) {
    // Try to extract the XML from HTML first, then from normalized text.
    QString xml = MxGraphRenderer::extractMxGraphXml(html);
    if (xml.isEmpty()) {
        xml = MxGraphRenderer::extractMxGraphXml(normalizedText);
    }
    if (xml.isEmpty()) {
        return {};
    }

    const QSize logicalSize = previewLogicalSize(itemScale);
    const QSize pixelSize = logicalSize * targetDpr;
    const bool dark = ThemeManager::instance()->isDark();
    return MxGraphRenderer::render(xml, pixelSize, targetDpr, dark);
}

ClipboardItem prepareItemForDisplayAndSave(const ClipboardItem &source) {
    ClipboardItem item(source);
    if (item.getContentType() == RichText
        && item.getPreviewKind() == TextPreview
        && item.hasThumbnail()) {
        item.setThumbnail(QPixmap());
        item.setThumbnailAvailableHint(false);
    }
    if (!item.hasThumbnail() && (item.getContentType() == Image
                                 || item.getContentType() == Office)) {
        const QPixmap thumbnail = buildCardThumbnail(item);
        if (!thumbnail.isNull()) {
            item.setThumbnail(thumbnail);
        }
    } else if (!item.hasThumbnail()
               && item.getContentType() == RichText
               && item.getPreviewKind() == VisualPreview) {
        const QPixmap thumbnail = buildRichTextThumbnail(item);
        if (!thumbnail.isNull()) {
            item.setThumbnail(thumbnail);
        }
    }
    return item;
}

} // namespace ThumbnailBuilder
