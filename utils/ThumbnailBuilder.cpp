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
#include <QAbstractTextDocumentLayout>
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

#include <cmath>

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
    return cardPreviewSize(itemScale);
}

// Unstyled text takes MPaste's own card text colour, not the platform palette's,
// which can be dark while MPaste is light (or the reverse).
void drawRichTextDocument(QTextDocument &document, QPainter *painter, const QSizeF &layoutSize) {
    QAbstractTextDocumentLayout::PaintContext paintContext;
    paintContext.clip = QRectF(QPointF(0, 0), layoutSize);
    const bool dark = ThemeManager::instance()->isDark();
    paintContext.palette.setColor(QPalette::Text, dark ? QColor(QStringLiteral("#E4E7EC")) : QColor(QStringLiteral("#30343B")));
    document.documentLayout()->draw(painter, paintContext);
}

QString richTextThumbnailStyleSheet(int itemScale) {
    const int scale = qMax(50, itemScale);
    const int bodyPaddingX = qMax(8, 12 * scale / 100);
    const int baseFontPx = qMax(12, 14 * scale / 100);
    return QStringLiteral(
        "html, body { margin: 0 !important; width: 100% !important; max-width: 100% !important; }"
        "body {"
        " padding: %1px %1px 0 %1px !important;"
        " font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', 'Segoe UI', 'Noto Sans', sans-serif !important;"
        " font-size: %2px !important;"
        " line-height: 1.34 !important;"
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
        "a { color: %3; }"
        "pre, code {"
        " white-space: pre-wrap !important;"
        " overflow-wrap: anywhere !important;"
        " word-break: break-word !important;"
        "}")
        .arg(bodyPaddingX)
        .arg(baseFontPx)
        // HTML import bakes the link colour in, so it is set here, readable on either card ground.
        .arg(ThemeManager::instance()->isDark() ? QStringLiteral("#7DB4F5") : QStringLiteral("#1F64C2"));
}

QString normalizeInlineTypography(QString tagStr) {
    // Rich text copied from Word/WPS often carries inline font-size /
    // line-height styles that make thumbnail previews look oversized
    // compared with plain-text cards.
    static const QRegularExpression typographyDecl(
        QStringLiteral(R"((?:^|;)\s*(?:font-size|line-height|mso-[a-z-]+)\s*:\s*[^;\"']*;?)"),
        QRegularExpression::CaseInsensitiveOption);
    tagStr.replace(typographyDecl, QStringLiteral(";"));

    static const QRegularExpression emptyStyleAttrSingle(
        QStringLiteral(R"(\sstyle\s*=\s*'[\s;]*')"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression emptyStyleAttrDouble(
        QStringLiteral(R"(\sstyle\s*=\s*"[\s;]*")"),
        QRegularExpression::CaseInsensitiveOption);
    tagStr.remove(emptyStyleAttrSingle);
    tagStr.remove(emptyStyleAttrDouble);
    return tagStr;
}

// A CSS colour as written inline: #rgb, #rrggbb, rgb()/rgba() or a named colour.
QColor parseInlineCssColor(const QString &value) {
    const QString v = value.trimmed();
    static const QRegularExpression rgbRx(
        QStringLiteral(R"(^rgba?\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*([\d.]+)\s*)?\)$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = rgbRx.match(v);
    if (m.hasMatch()) {
        QColor color(qBound(0, m.captured(1).toInt(), 255), qBound(0, m.captured(2).toInt(), 255),
                     qBound(0, m.captured(3).toInt(), 255));
        if (!m.captured(4).isEmpty()) {
            color.setAlphaF(qBound(0.0, m.captured(4).toDouble(), 1.0));
        }
        return color;
    }
    return QColor::fromString(v);
}

qreal relativeLuminance(const QColor &color) {
    auto channel = [](qreal c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

qreal contrastRatio(const QColor &a, const QColor &b) {
    const qreal la = relativeLuminance(a);
    const qreal lb = relativeLuminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

// Inline colours are written for the page they were copied from, which may be
// light or dark whatever MPaste's theme is. Against the card (or the preview
// window, the same ground) they are adapted:
// - a near-white or near-black background is that page's own ground, and goes;
//   coloured ones (highlights, code blocks) stay;
// - a text colour that reads poorly on what is behind it (its own remaining
//   background, else the card) takes the theme's text colour; colours that read
//   (links, emphasis) stay as they are.
QString adaptInlineColorsToCard(const QString &tagStr) {
    const bool dark = ThemeManager::instance()->isDark();
    const QColor lightInk(QStringLiteral("#E4E7EC"));
    const QColor darkInk(QStringLiteral("#30343B"));
    QColor ground = dark ? QColor(0x17, 0x18, 0x1E) : QColor(0xFC, 0xFC, 0xFE);

    QString result = tagStr;
    static const QRegularExpression backgroundRx(
        QStringLiteral(R"((?:(?<=[\s;"'])|^)(background(?:-color)?\s*:\s*([^;"']+);?))"),
        QRegularExpression::CaseInsensitiveOption);
    QList<QRegularExpressionMatch> pageGrounds;
    for (QRegularExpressionMatchIterator it = backgroundRx.globalMatch(result); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QColor background = parseInlineCssColor(m.captured(2));
        if (!background.isValid() || background.alpha() == 0) {
            continue;
        }
        const qreal luminance = relativeLuminance(background);
        if (luminance > 0.85 || luminance < 0.02) {
            pageGrounds.prepend(m); // removed back to front, so earlier offsets hold
        } else {
            ground = background;
        }
    }
    for (const QRegularExpressionMatch &m : pageGrounds) {
        result.remove(m.capturedStart(1), m.capturedLength(1));
    }

    static const QRegularExpression colorRx(
        QStringLiteral(R"((?:(?:[\s;"']|^)color\s*[:=]\s*["']?\s*)(#[0-9a-fA-F]{3,6}\b|rgba?\s*\([^)]*\)|[a-zA-Z]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QString ink = (contrastRatio(lightInk, ground) >= contrastRatio(darkInk, ground) ? lightInk : darkInk).name();
    const QString source = result;
    int offset = 0;
    for (QRegularExpressionMatchIterator it = colorRx.globalMatch(source); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QColor color = parseInlineCssColor(m.captured(1));
        if (!color.isValid() || color.alpha() == 0 || contrastRatio(color, ground) >= 3.0) {
            continue;
        }
        result.replace(m.capturedStart(1) + offset, m.capturedLength(1), ink);
        offset += ink.size() - m.capturedLength(1);
    }
    return result;
}

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
                    tagStr = adaptInlineColorsToCard(tagStr);
                    tagStr = normalizeInlineTypography(tagStr);
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

QString richTextHtmlForThumbnail(QString html, int itemScale) {
    html = simplifyHtmlForRendering(html);
    const QString styleTag = QStringLiteral("<style>%1</style>").arg(richTextThumbnailStyleSheet(itemScale));
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
                                        const QByteArray &imageBytes,
                                        int itemScale) {
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
    document.setDefaultStyleSheet(richTextThumbnailStyleSheet(itemScale));
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

// Rich text with pictures, trimmed to what it draws: scaled whole into the card
// and set on its top-left, where text cards start. Cut to fill instead, a wide
// shape came out as a close-up of its middle and text lost its first letters.
QImage scaleFitToTarget(const QImage &source, const QSize &pixelTargetSize) {
    if (source.isNull() || !pixelTargetSize.isValid()) {
        return source;
    }

    const QImage scaled = source.scaled(pixelTargetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return source;
    }

    QImage canvas(pixelTargetSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.drawImage(QRect(QPoint(0, 0), scaled.size()), scaled);
    painter.end();
    return canvas;
}

// True when nothing in the render is visible (a page that is only an <img>,
// which the render leaves out).
static bool drawsNothing(const QImage &render) {
    const QImage image = render.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) > 6) {
                return false;
            }
        }
    }
    return true;
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

    QFont font(QStringLiteral("Microsoft YaHei UI"));
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

    QFont font(QStringLiteral("Microsoft YaHei UI"));
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

    QPixmap thumbnail = QPixmap::fromImage(cardPictureThumbnail(fullImage.toImage(), pixelTargetSize));
    if (thumbnail.isNull()) {
        return QPixmap();
    }
    thumbnail.setDevicePixelRatio(thumbnailDpr);
    return thumbnail;
}

QPixmap buildRichTextThumbnail(const ClipboardItem &item) {
    const QMimeData *mimeData = item.getMimeData();
    if (!mimeData || !mimeData->hasHtml()) {
        return QPixmap();
    }

    const QString html = item.getHtml();
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
    configureRichTextThumbnailDocument(document, html, imageSource, imageBytes,
                                       MPasteSettings::getInst()->getItemScale());
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
    drawRichTextDocument(document, &painter, layoutSize);
    painter.end();

    if (!richTextHtmlHasImageContent(html)) {
        snapshot.setDevicePixelRatio(thumbnailDpr);
        return snapshot;
    }
    // Only a picture (a WPS shape saved as rich text): show the one it carries.
    if (drawsNothing(snapshot.toImage()) && !imageBytes.isEmpty()) {
        const QImage picture = buildCardThumbnailImageFromBytes(imageBytes, thumbnailDpr,
                                                                MPasteSettings::getInst()->getItemScale());
        if (!picture.isNull()) {
            QPixmap pixmap = QPixmap::fromImage(picture);
            pixmap.setDevicePixelRatio(thumbnailDpr);
            return pixmap;
        }
    }

    QImage trimmed = trimTransparentPadding(snapshot.toImage(), qRound(2 * thumbnailDpr));
    QImage scaled = scaleFitToTarget(trimmed, pixelTargetSize);
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
        // Decode at the size cardPictureThumbnail will use.
        const QSize scaledSize = sourceSize.scaled(pixelTargetSize,
            cardPictureCovers(sourceSize, pixelTargetSize) ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
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

    QImage thumbnail = cardPictureThumbnail(decoded, pixelTargetSize);
    if (thumbnail.isNull()) {
        return QImage();
    }
    thumbnail.setDevicePixelRatio(targetDpr);
    return thumbnail;
}

QImage buildRichTextThumbnailImageFromHtml(const QString &rawHtml, const QByteArray &imageBytes, qreal thumbnailDpr, int itemScale) {
    const QString html = cleanClipboardHtml(rawHtml);
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
    configureRichTextThumbnailDocument(document, truncatedHtml, imageSource, imageBytes, itemScale);
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
    drawRichTextDocument(document, &painter, layoutSize);
    painter.end();

    if (!richTextHtmlHasImageContent(truncatedHtml)) {
        snapshot.setDevicePixelRatio(thumbnailDpr);
        return snapshot;
    }
    // Only a picture (a WPS shape saved as rich text): show the one it carries.
    if (drawsNothing(snapshot) && !imageBytes.isEmpty()) {
        const QImage picture = buildCardThumbnailImageFromBytes(imageBytes, thumbnailDpr, itemScale);
        if (!picture.isNull()) {
            return picture;
        }
    }

    QImage trimmed = trimTransparentPadding(snapshot, qRound(2 * thumbnailDpr));
    QImage scaled = scaleFitToTarget(trimmed, pixelTargetSize);
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
        QPixmap thumbnail = buildCardThumbnail(item);
        if (thumbnail.isNull()
            && item.getContentType() == Office
            && !item.getHtml().isEmpty()) {
            thumbnail = buildRichTextThumbnail(item);
        }
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
