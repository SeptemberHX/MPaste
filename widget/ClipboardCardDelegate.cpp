// input: Depends on ClipboardCardDelegate.h, card metrics, and ClipboardItem display data.
// output: Paints clipboard cards and loads bounded file icons using logical size plus explicit DPR.
// pos: Widget-layer delegate implementation that replaces per-item QWidget rendering.
// update: If I change, update this header block and my folder README.md (smaller card typography + file image thumbnails + improved file previews + footer height tweak + link preview caption trimmed + header spacing + link url shortcut spacing + custom alias header line + pinned badge + rich text fill + data-layer preview kind + async file preview caching).
// note: Adjusted card palette for dark theme and moved heavy file preview work out of paint.
#include "ClipboardCardDelegate.h"

#include <cmath>

#include <QAbstractItemView>
#include <QApplication>
#include <QCache>
#include <QDebug>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPixmapCache>
#include <QRadialGradient>
#include <QRunnable>
#include <QRegularExpression>
#include <QSet>
#include <QTextLayout>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "ClipboardBoardModel.h"
#include "CardMetrics.h"
#include "CardTheme.h"
#include "cardrenderer/CardBodyRenderer.h"
#include "cardrenderer/ImageCardBody.h"
#include "cardrenderer/OfficeCardBody.h"
#include "cardrenderer/RichTextCardBody.h"
#include "cardrenderer/ColorCardBody.h"
#include "cardrenderer/LinkCardBody.h"
#include "cardrenderer/FileCardBody.h"
#include "cardrenderer/TextCardBody.h"
#include "utils/MPasteSettings.h"
#include "utils/ThemeManager.h"
#include "utils/MxGraphRenderer.h"

namespace {
constexpr float kPi = 3.14159265358979323846f;

// CardData is defined in ClipboardCardDelegate.h

QColor blendColor(const QColor &from, const QColor &to, qreal factor) {
    const qreal t = qBound(0.0, factor, 1.0);
    return QColor(
        qRound(from.red() + (to.red() - from.red()) * t),
        qRound(from.green() + (to.green() - from.green()) * t),
        qRound(from.blue() + (to.blue() - from.blue()) * t),
        qRound(from.alpha() + (to.alpha() - from.alpha()) * t));
}

// The app's hue at one shared saturation ceiling and lightness, so a row of
// cards from different apps reads as one calm set instead of a paint box.
QColor cardToneForColor(const QColor &color, bool darkTheme) {
    const QColor hsl = color.toHsl();
    qreal hue = qMax<qreal>(0.0, hsl.hslHueF());
    // Darkened yellow reads as olive: lean yellows (40-68 deg) towards amber instead.
    if (hue > 40.0 / 360.0 && hue < 68.0 / 360.0) {
        hue = 36.0 / 360.0 + (hue - 40.0 / 360.0) * 0.25;
    }
    const qreal saturation = hsl.hslSaturationF() < 0.08 ? 0.0 : hsl.hslSaturationF();
    // Light theme keeps a deep band too (white text on it); its body is the light tint.
    return darkTheme ? QColor::fromHslF(hue, qMin<qreal>(saturation, 0.62), 0.30)
                     : QColor::fromHslF(hue, qMin<qreal>(saturation, 0.62), 0.38);
}

// Card corner radius; the selection ring follows the same curve.
qreal cardRadiusForScale(int scale) {
    return qMax(10, 14 * scale / 100);
}

qreal relativeLuminance(const QColor &color) {
    auto channel = [](qreal c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

// Items from windows without an application icon carry the generic unknown.svg
// (embedded in older history too); those get a type monogram instead.
bool isPlaceholderIcon(const QPixmap &icon) {
    return CardRenderUtils::isPlaceholderIcon(icon);
}

// Absolute and short, so a cached card never shows a stale "3 minutes ago".
QString compactTimeLabel(const QDateTime &time) {
    if (!time.isValid()) {
        return QString();
    }
    const bool chinese = QLocale::system().language() == QLocale::Chinese;
    const QDate today = QDate::currentDate();
    const QString clock = time.toString(QStringLiteral("HH:mm"));
    if (time.date() == today) {
        return clock;
    }
    if (time.date() == today.addDays(-1)) {
        return (chinese ? QStringLiteral("昨天 ") : QStringLiteral("Yesterday ")) + clock;
    }
    if (time.date().year() == today.year()) {
        return QLocale::system().toString(time.date(), chinese ? QStringLiteral("M月d日") : QStringLiteral("MMM d"))
            + QLatin1Char(' ') + clock;
    }
    return QLocale::system().toString(time.date(), QLocale::ShortFormat);
}

QString typeLabelForCard(const CardData &card) {
    switch (card.contentType) {
        case Text:
            if (ColorCardBody::colorFromText(card.normalizedText).isValid()) {
                return QObject::tr("Color");
            }
            if (CardRenderUtils::singleWebUrl(card.normalizedText).isValid()) {
                return QObject::tr("Link");
            }
            return QObject::tr("Plain Text");
        case Link:
            return QObject::tr("Link");
        case Image:
            return QObject::tr("Image");
        case Office:
            return QObject::tr("Office Shape");
        case RichText:
            return QObject::tr("Rich Text");
        case File:
            return card.normalizedUrls.size() > 1
                ? QStringLiteral("%1 %2").arg(card.normalizedUrls.size()).arg(QObject::tr("Files"))
                : QObject::tr("File");
        case Color:
            return QObject::tr("Color");
        case All:
            break;
    }
    return QObject::tr("Item");
}

QString countLabelForCard(const CardData &card) {
    switch (card.contentType) {
        case Image:
        case Office:
            return card.imageSize.isValid()
                ? QStringLiteral("%1 \u00D7 %2 %3").arg(card.imageSize.width()).arg(card.imageSize.height()).arg(QObject::tr("Pixels"))
                : QString();
        case Text:
            if (ColorCardBody::colorFromText(card.normalizedText).isValid()) {
                return QString();
            }
            if (const QUrl url = CardRenderUtils::singleWebUrl(card.normalizedText); url.isValid()) {
                return CardRenderUtils::displayHost(url);
            }
            return QStringLiteral("%1 %2").arg(card.textLength).arg(QObject::tr("Characters"));
        case RichText:
            return QStringLiteral("%1 %2").arg(card.textLength).arg(QObject::tr("Characters"));
        case Link: {
            if (!card.url.isEmpty()) {
                return card.url;
            }
            if (!card.normalizedUrls.isEmpty()) {
                const QUrl &first = card.normalizedUrls.first();
                return first.isLocalFile() ? first.toLocalFile() : first.toDisplayString(QUrl::PreferLocalFile);
            }
            return card.normalizedText.left(256).trimmed();
        }
        case File:
            if (card.normalizedUrls.size() > 1) {
                return QStringLiteral("%1 %2").arg(card.normalizedUrls.size()).arg(QObject::tr("Files"));
            }
            if (card.normalizedUrls.size() == 1) {
                const QUrl url = card.normalizedUrls.front();
                if (url.isLocalFile()) {
                    // Size, then the folder with the home directory shortened to ~.
                    const QFileInfo info(url.toLocalFile());
                    QString folder = QDir::toNativeSeparators(info.absolutePath());
                    const QString home = QDir::toNativeSeparators(QDir::homePath());
                    if (folder == home || folder.startsWith(home + QDir::separator())) {
                        folder = QStringLiteral("~") + folder.mid(home.size());
                    }
                    if (info.isFile()) {
                        return QLocale().formattedDataSize(info.size(), 1, QLocale::DataSizeTraditionalFormat)
                            + QStringLiteral(" \u00B7 ") + folder;
                    }
                    return folder;
                }
                return url.toDisplayString(QUrl::PreferLocalFile);
            }
            return QString();
        case Color:
        case All:
            break;
    }
    return QString();
}

QString compactHostLabel(const QUrl &url) {
    QString host = url.host().trimmed();
    if (host.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) {
        host.remove(0, 4);
    }
    if (!host.isEmpty()) {
        return host;
    }

    const QString text = url.toString(QUrl::RemoveScheme | QUrl::RemoveUserInfo | QUrl::RemoveFragment).trimmed();
    return text.isEmpty() ? QStringLiteral("link") : text;
}

QString placeholderMonogram(const QUrl &url, const QString &title) {
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

    QString token = extractLetters(compactHostLabel(url));
    if (token.isEmpty()) {
        token = extractLetters(title);
    }
    if (token.isEmpty()) {
        token = QStringLiteral("L");
    }
    return token.left(2);
}

QPair<QColor, QColor> placeholderPalette(const QString &seed) {
    const uint hash = qHash(seed);
    const int hueA = static_cast<int>(hash % 360);
    const int hueB = static_cast<int>((hueA + 28 + (hash % 71)) % 360);
    return qMakePair(QColor::fromHsl(hueA, 130, 152),
                     QColor::fromHsl(hueB, 122, 170));
}

QString fallbackHeadline(const QString &title, const QUrl &url) {
    const QString trimmedTitle = title.trimmed();
    if (!trimmedTitle.isEmpty()) {
        return trimmedTitle;
    }
    return compactHostLabel(url);
}

QString scaledPixmapCacheKey(const QPixmap &pixmap, const QSize &targetLogicalSize, qreal targetDpr) {
    return QStringLiteral("%1:%2x%3@%4")
        .arg(QString::number(static_cast<qulonglong>(pixmap.cacheKey())))
        .arg(targetLogicalSize.width())
        .arg(targetLogicalSize.height())
        .arg(qRound(targetDpr * 100.0));
}

QString filePreviewCacheKey(const QString &filePath, const QFileInfo &info, const QSize &targetLogicalSize, qreal targetDpr) {
    return QStringLiteral("%1:%2:%3x%4@%5")
        .arg(filePath)
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(targetLogicalSize.width())
        .arg(targetLogicalSize.height())
        .arg(qRound(targetDpr * 100.0));
}

QString linkFallbackCacheKey(const QUrl &url,
                             const QString &title,
                             const QSize &targetSize,
                             qreal devicePixelRatio,
                             const QPixmap &favicon) {
    return QStringLiteral("%1|%2|%3|%4x%5@%6")
        .arg(url.toString())
        .arg(title)
        .arg(QString::number(static_cast<qulonglong>(favicon.cacheKey())))
        .arg(targetSize.width())
        .arg(targetSize.height())
        .arg(qRound(devicePixelRatio * 100.0));
}

QColor computeDominantHeaderColor(const QPixmap &iconPixmap) {
    QPixmap icon = iconPixmap;
    if (icon.isNull()) {
        icon = QPixmap(":/resources/resources/unknown.svg");
    }

    const QImage image = icon.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return QColor(QStringLiteral("#4A5F7A"));
    }

    constexpr int kHueBins = 12;
    float hueBins[kHueBins] = {};
    float hueSumSin[kHueBins] = {};
    float hueSumCos[kHueBins] = {};
    float satSum[kHueBins] = {};
    int binCount[kHueBins] = {};
    int totalColorful = 0;
    int totalPixels = 0;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color(image.pixel(x, y));
            if (color.alpha() < 10) {
                continue;
            }
            ++totalPixels;

            float h = 0.0f;
            float s = 0.0f;
            float l = 0.0f;
            color.getHslF(&h, &s, &l);
            if (s < 0.15f || l < 0.1f || l > 0.9f) {
                continue;
            }

            ++totalColorful;
            const int bin = qBound(0, static_cast<int>(h * kHueBins), kHueBins - 1);
            const float weight = s;
            hueBins[bin] += weight;
            hueSumSin[bin] += weight * std::sin(h * 2.0f * kPi);
            hueSumCos[bin] += weight * std::cos(h * 2.0f * kPi);
            satSum[bin] += s;
            ++binCount[bin];
        }
    }

    if (totalPixels <= 0 || totalColorful <= totalPixels * 0.05f) {
        return QColor(QStringLiteral("#4A5F7A"));
    }

    int bestBin = 0;
    for (int i = 1; i < kHueBins; ++i) {
        if (hueBins[i] > hueBins[bestBin]) {
            bestBin = i;
        }
    }

    float avgHue = std::atan2(hueSumSin[bestBin], hueSumCos[bestBin]) / (2.0f * kPi);
    if (avgHue < 0.0f) {
        avgHue += 1.0f;
    }
    const float avgSat = satSum[bestBin] / qMax(1, binCount[bestBin]);
    const float s = qBound(0.35f, avgSat * 1.5f, 0.75f);
    const float l = 0.45f;
    return QColor::fromHslF(avgHue, s, l);
}

void drawShadow(QPainter *painter, const QRectF &cardRect, qreal radius) {
    painter->save();
    painter->setPen(Qt::NoPen);

    const struct {
        QPointF offset;
        int alpha;
    } layers[] = {
        {QPointF(1.5, 2.0), 22},
        {QPointF(3.0, 4.0), 18},
        {QPointF(4.5, 6.0), 12}
    };

    for (const auto &layer : layers) {
        painter->setBrush(QColor(0, 0, 0, layer.alpha));
        painter->drawRoundedRect(cardRect.translated(layer.offset), radius, radius);
    }
    painter->restore();
}

QPixmap buildLinkFallbackPreviewUncached(const QUrl &url, const QString &title, const QSize &targetSize, qreal devicePixelRatio,
                                         const QPixmap &favicon = QPixmap()) {
    if (!targetSize.isValid()) {
        return {};
    }

    const QSize pixelTargetSize = targetSize * devicePixelRatio;
    QPixmap canvas(pixelTargetSize);
    canvas.fill(Qt::transparent);
    canvas.setDevicePixelRatio(devicePixelRatio);

    const QString hostLabel = compactHostLabel(url);
    const QString monogram = placeholderMonogram(url, title);
    const QString headline = fallbackHeadline(title, url);
    const auto palette = placeholderPalette(hostLabel + title);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds(QPointF(0, 0), QSizeF(targetSize));
    QLinearGradient background(bounds.topLeft(), bounds.bottomRight());
    background.setColorAt(0.0, palette.first.lighter(120));
    background.setColorAt(0.48, palette.second);
    background.setColorAt(1.0, palette.first.darker(116));
    painter.fillRect(bounds, background);

    QRadialGradient glow(bounds.topRight() + QPointF(-bounds.width() * 0.18, bounds.height() * 0.12),
                         bounds.width() * 0.88);
    QColor glowColor(255, 255, 255, 138);
    glow.setColorAt(0.0, glowColor);
    glow.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(bounds, glow);

    const QRectF browserRect = bounds.adjusted(14.0, 12.0, -14.0, -12.0);
    QPainterPath browserPath;
    browserPath.addRoundedRect(browserRect, 18.0, 18.0);
    painter.fillPath(browserPath, QColor(255, 255, 255, 54));
    painter.strokePath(browserPath, QPen(QColor(255, 255, 255, 52), 1.2));

    const QRectF toolbarRect(browserRect.left() + 10.0, browserRect.top() + 10.0,
                             browserRect.width() - 20.0, 26.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 58));
    painter.drawRoundedRect(toolbarRect, 13.0, 13.0);

    const qreal dotY = toolbarRect.center().y();
    const qreal firstDotX = toolbarRect.left() + 12.0;
    const QColor dotColors[3] = {
        QColor(255, 120, 120, 200),
        QColor(255, 214, 102, 190),
        QColor(107, 203, 119, 190)
    };
    for (int i = 0; i < 3; ++i) {
        painter.setBrush(dotColors[i]);
        painter.drawEllipse(QPointF(firstDotX + i * 10.0, dotY), 2.7, 2.7);
    }

    QFont chipFont = painter.font();
    CardRenderUtils::applyUiFontDefaults(chipFont);
    chipFont.setBold(true);
    chipFont.setPointSizeF(qMax(8.0, bounds.height() * 0.048));
    painter.setFont(chipFont);
    const QString chipText = hostLabel.toUpper();
    QFontMetricsF chipMetrics(chipFont);
    const qreal chipPaddingX = 10.0;
    const qreal chipHeight = chipMetrics.height() + 8.0;
    const qreal chipWidth = qMin(toolbarRect.width() - 54.0, chipMetrics.horizontalAdvance(chipText) + chipPaddingX * 2.0);
    const QRectF chipRect(toolbarRect.right() - chipWidth - 10.0,
                          toolbarRect.center().y() - chipHeight / 2.0,
                          chipWidth, chipHeight);
    painter.setBrush(QColor(255, 255, 255, 64));
    painter.drawRoundedRect(chipRect, chipHeight / 2.0, chipHeight / 2.0);
    painter.setPen(QColor(255, 255, 255, 220));
    painter.drawText(chipRect, Qt::AlignCenter, chipMetrics.elidedText(chipText, Qt::ElideRight, chipRect.width() - 10.0));

    const QRectF heroRect(browserRect.left() + 18.0, toolbarRect.bottom() + 16.0,
                          browserRect.width() - 36.0, browserRect.height() - toolbarRect.height() - 34.0);

    QRadialGradient heroGlow(heroRect.center() + QPointF(0.0, -heroRect.height() * 0.12),
                             qMin(heroRect.width(), heroRect.height()) * 0.52);
    heroGlow.setColorAt(0.0, QColor(255, 255, 255, 116));
    heroGlow.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(heroRect, heroGlow);

    const bool hasFavicon = !favicon.isNull();
    const qreal badgeScale = hasFavicon ? 0.56 : 0.38;
    const qreal badgeSize = qMin(heroRect.width(), heroRect.height()) * badgeScale;
    const QRectF badgeRect(heroRect.center().x() - badgeSize / 2.0,
                           heroRect.top() + heroRect.height() * 0.10,
                           badgeSize, badgeSize);
    painter.setBrush(QColor(255, 255, 255, 214));
    painter.drawEllipse(badgeRect);

    QPen ringPen(QColor(72, 92, 110, 44));
    ringPen.setWidthF(1.4);
    painter.setPen(ringPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(badgeRect.adjusted(-4.0, -4.0, 4.0, 4.0));

    if (hasFavicon) {
        const qreal iconScale = 0.60;
        const QSize iconLogicalSize(qMax(8, qRound(badgeSize * iconScale)),
                                    qMax(8, qRound(badgeSize * iconScale)));
        QPixmap iconPixmap = favicon;
        if (!iconLogicalSize.isEmpty()) {
            iconPixmap = iconPixmap.scaled(iconLogicalSize * devicePixelRatio,
                                           Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
            iconPixmap.setDevicePixelRatio(devicePixelRatio);
        }
        const QRectF iconRect(badgeRect.center().x() - iconLogicalSize.width() / 2.0,
                              badgeRect.center().y() - iconLogicalSize.height() / 2.0,
                              iconLogicalSize.width(),
                              iconLogicalSize.height());
        painter.drawPixmap(iconRect, iconPixmap, QRectF());
    } else {
        QFont monoFont = painter.font();
        CardRenderUtils::applyMonoFontDefaults(monoFont);
        monoFont.setBold(true);
        monoFont.setPointSizeF(qMax(18.0, badgeSize * 0.26));
        monoFont.setLetterSpacing(QFont::PercentageSpacing, 105);
        painter.setFont(monoFont);
        painter.setPen(QColor(58, 72, 86));
        painter.drawText(badgeRect, Qt::AlignCenter, monogram);
    }

    const QRectF headlineRect(heroRect.left(), badgeRect.bottom() + 16.0, heroRect.width(), 34.0);
    QFont headlineFont = painter.font();
#ifdef Q_OS_WIN
    headlineFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
#endif
    headlineFont.setBold(true);
    headlineFont.setPointSizeF(qMax(9.0, bounds.height() * 0.048));
    painter.setFont(headlineFont);
    painter.setPen(QColor(255, 255, 255, 235));
    const QFontMetricsF headlineMetrics(headlineFont);
    painter.drawText(headlineRect, Qt::AlignCenter,
                     headlineMetrics.elidedText(headline, Qt::ElideRight, headlineRect.width()));

    const QRectF captionRect(heroRect.left() + 16.0, headlineRect.bottom() + 6.0,
                             heroRect.width() - 32.0, 22.0);

    painter.setBrush(QColor(255, 255, 255, 48));
    painter.setPen(Qt::NoPen);
    const qreal lineWidth = heroRect.width() * 0.42;
    painter.drawRoundedRect(QRectF(heroRect.center().x() - lineWidth / 2.0,
                                   captionRect.bottom() + 14.0,
                                   lineWidth, 6.0), 3.0, 3.0);
    painter.drawRoundedRect(QRectF(heroRect.center().x() - lineWidth * 0.34,
                                   captionRect.bottom() + 26.0,
                                   lineWidth * 0.68, 6.0), 3.0, 3.0);

    return canvas;
}

QPixmap starPixmap(const QSize &size) {
    static QCache<QString, QPixmap> cache(1024);
    const QString cacheKey = QStringLiteral("star:%1x%2").arg(size.width()).arg(size.height());
    if (QPixmap *cached = cache.object(cacheKey)) {
        return *cached;
    }

    const QPixmap pixmap = QIcon(QStringLiteral(":/resources/resources/star_filled.svg")).pixmap(size);
    if (!pixmap.isNull()) {
        const int cacheCost = qMax(1, (pixmap.width() * pixmap.height() * 4) / 1024);
        cache.insert(cacheKey, new QPixmap(pixmap), cacheCost);
    }
    return pixmap;
}

QPixmap pinPixmap(const QSize &size, bool dark) {
    static QCache<QString, QPixmap> cache(1024);
    const QString path = dark
        ? QStringLiteral(":/resources/resources/pin_light.svg")
        : QStringLiteral(":/resources/resources/pin.svg");
    const QString cacheKey = QStringLiteral("pin:%1:%2x%3").arg(path).arg(size.width()).arg(size.height());
    if (QPixmap *cached = cache.object(cacheKey)) {
        return *cached;
    }

    const QPixmap pixmap = QIcon(path).pixmap(size);
    if (!pixmap.isNull()) {
        const int cacheCost = qMax(1, (pixmap.width() * pixmap.height() * 4) / 1024);
        cache.insert(cacheKey, new QPixmap(pixmap), cacheCost);
    }
    return pixmap;
}

QPixmap loadLocalFileIconSync(const QString &filePath, const QSize &targetLogicalSize, qreal targetDpr) {
    if (filePath.isEmpty() || !targetLogicalSize.isValid()) {
        return {};
    }

    const QFileInfo info(filePath);
    if (!info.exists()) {
        return {};
    }

    const QSize pixelTargetSize = targetLogicalSize * qMax<qreal>(1.0, targetDpr);
    if (!pixelTargetSize.isValid()) {
        return {};
    }

    QFileIconProvider provider;
    QIcon icon = provider.icon(info);
    QPixmap pixmap = icon.pixmap(targetLogicalSize, qMax<qreal>(1.0, targetDpr));
    if (pixmap.isNull()) {
        return {};
    }
    // Some platform icon engines return a larger native bitmap than requested.
    if (pixmap.width() > pixelTargetSize.width() || pixmap.height() > pixelTargetSize.height())
        pixmap = pixmap.scaled(pixelTargetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(qMax<qreal>(1.0, targetDpr));
    return pixmap;
}


bool isLikelyImageFile(const QString &filePath) {
    if (filePath.isEmpty()) {
        return false;
    }

    // Check suffix BEFORE stat to avoid blocking on network/virtual paths.
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    if (suffix.isEmpty()) {
        return false;
    }

    static QSet<QString> supportedSuffixes;
    static bool initialized = false;
    if (!initialized) {
        const QList<QByteArray> formats = QImageReader::supportedImageFormats();
        for (const QByteArray &format : formats) {
            supportedSuffixes.insert(QString::fromLatin1(format).toLower());
        }
        initialized = true;
    }

    return supportedSuffixes.contains(suffix);
}

QImage loadLocalImageThumbnailImageSync(const QString &filePath, const QSize &targetLogicalSize, qreal targetDpr) {
    if (!isLikelyImageFile(filePath) || !targetLogicalSize.isValid()) {
        return {};
    }

    const QFileInfo info(filePath);
    const QSize pixelTargetSize = targetLogicalSize * qMax<qreal>(1.0, targetDpr);
    if (!pixelTargetSize.isValid()) {
        return {};
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        const QSize scaledSize = sourceSize.scaled(pixelTargetSize, Qt::KeepAspectRatioByExpanding);
        if (scaledSize.isValid()) {
            reader.setScaledSize(scaledSize);
        }
    }

    const QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }

    return image;
}


QPixmap buildHeaderIconPixmapUncached(const QPixmap &sourcePixmap, const QSize &targetLogicalSize, qreal targetDpr) {
    if (!targetLogicalSize.isValid()) {
        return {};
    }

    QPixmap icon = sourcePixmap;
    if (icon.isNull()) {
        icon = QPixmap(QStringLiteral(":/resources/resources/unknown.svg"));
    }

    const QSize pixelTargetSize = targetLogicalSize * qMax<qreal>(1.0, targetDpr);
    icon = icon.scaled(pixelTargetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    icon.setDevicePixelRatio(qMax<qreal>(1.0, targetDpr));

    if (icon.toImage().isNull() || icon.toImage().format() == QImage::Format_Invalid) {
        const QImage image = icon.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        icon = QPixmap::fromImage(image);
        icon.setDevicePixelRatio(qMax<qreal>(1.0, targetDpr));
    }

    return icon;
}
}

ClipboardCardDelegate::ClipboardCardDelegate(const QColor &borderColor, QObject *parent)
    : QStyledItemDelegate(parent),
      cachedTheme_(CardTheme::forCurrentTheme()),
      borderColor_(borderColor),
      headerIconCache_(60),
      linkFallbackCache_(60),
      cardPixmapCache_(18),
      localImageThumbnailCache_(60),
      localFileIconCache_(60),
      imageRenderer_(std::make_unique<ImageCardBody>()),
      officeRenderer_(std::make_unique<OfficeCardBody>()),
      richTextRenderer_(std::make_unique<RichTextCardBody>()),
      colorRenderer_(std::make_unique<ColorCardBody>()),
      linkRenderer_(std::make_unique<LinkCardBody>()),
      fileRenderer_(std::make_unique<FileCardBody>()),
      textRenderer_(std::make_unique<TextCardBody>()) {
    previewTaskPool_.setMaxThreadCount(qBound(1, QThread::idealThreadCount() / 2, 4));
    previewTaskPool_.setExpiryTimeout(15000);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](bool) {
        cachedTheme_ = CardTheme::forCurrentTheme();
    });
}

ClipboardCardDelegate::~ClipboardCardDelegate() {
    previewTaskPool_.waitForDone();
}

const CardBodyRenderer &ClipboardCardDelegate::bodyRendererForType(ContentType type) const {
    switch (type) {
        case Image:    return *imageRenderer_;
        case Office:   return *officeRenderer_;
        case RichText: return *richTextRenderer_;
        case Color:    return *colorRenderer_;
        case Link:     return *linkRenderer_;
        case File:     return *fileRenderer_;
        case Text:
        case All:
        default:                      return *textRenderer_;
    }
}

void ClipboardCardDelegate::setLoadingPhase(int phase) {
    loadingPhase_ = phase;
}

void ClipboardCardDelegate::clearIntermediateCaches() {
    headerColorCache_.clear();
    headerIconCache_.clear();
    linkFallbackCache_.clear();
    CardRenderUtils::clearCoverPixmapCache();
    localImageThumbnailCache_.clear();
    // Keep localFileIconCache_ — file type icons are small, come from the
    // OS, and are needed for cache-miss re-renders of File cards.
    pendingLocalImageThumbnailKeys_.clear();
    failedLocalImageThumbnailKeys_.clear();
    pendingLocalFileIconKeys_.clear();
    failedLocalFileIconKeys_.clear();
    pendingFileIconRequests_.clear();
}

void ClipboardCardDelegate::clearVisualCaches() {
    clearIntermediateCaches();
    cardPixmapCache_.clear();
    QPixmapCache::clear();
    cachedTheme_ = CardTheme::forCurrentTheme();
}

bool ClipboardCardDelegate::isCardCached(const QString &name) const {
    return cardPixmapCache_.contains(name);
}

void ClipboardCardDelegate::invalidateCard(const QString &name) {
    cardPixmapCache_.remove(name);
}

void ClipboardCardDelegate::markOcrPending(const QString &name) {
    ocrPendingNames_.insert(name);
    invalidateCard(name);
}

void ClipboardCardDelegate::clearOcrPending(const QString &name) {
    ocrPendingNames_.remove(name);
    invalidateCard(name);
}

bool ClipboardCardDelegate::isOcrPending(const QString &name) const {
    return ocrPendingNames_.contains(name);
}

QString ClipboardCardDelegate::cacheMemoryStats() const {
    // Estimate bytes for a QCache<QString, QPixmap>.
    auto estimateCache = [](const QCache<QString, QPixmap> &cache) -> qint64 {
        qint64 bytes = 0;
        const auto keys = cache.keys();
        for (const QString &key : keys) {
            if (const QPixmap *pm = cache.object(key)) {
                bytes += static_cast<qint64>(pm->width()) * pm->height() * 4;
            }
        }
        return bytes;
    };

    QStringList lines;
    lines << QStringLiteral("  cardPixmapCache: %1 items, %2 KB")
                 .arg(cardPixmapCache_.size()).arg(estimateCache(cardPixmapCache_) / 1024);
    lines << QStringLiteral("  headerIconCache: %1 items, %2 KB")
                 .arg(headerIconCache_.size()).arg(estimateCache(headerIconCache_) / 1024);
    lines << QStringLiteral("  linkFallbackCache: %1 items, %2 KB")
                 .arg(linkFallbackCache_.size()).arg(estimateCache(linkFallbackCache_) / 1024);
    lines << QStringLiteral("  localImageThumbCache: %1 items, %2 KB")
                 .arg(localImageThumbnailCache_.size()).arg(estimateCache(localImageThumbnailCache_) / 1024);
    lines << QStringLiteral("  localFileIconCache: %1 items, %2 KB")
                 .arg(localFileIconCache_.size()).arg(estimateCache(localFileIconCache_) / 1024);
    lines << QStringLiteral("  QPixmapCache limit: %1 KB").arg(QPixmapCache::cacheLimit());
    return lines.join(QLatin1Char('\n'));
}

void ClipboardCardDelegate::preRenderAll(QAbstractItemModel *model, const QStyleOptionViewItem &baseOption, int maxRows) {
    if (!model) {
        return;
    }
    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize outerSize = cardOuterSizeForScale(scale);
    const qreal paintDpr = qMax(1.0, static_cast<qreal>(baseOption.decorationSize.width()));
    const int rowCount = maxRows > 0 ? qMin(maxRows, model->rowCount()) : model->rowCount();

    for (int row = 0; row < rowCount; ++row) {
        const QModelIndex index = model->index(row, 0);
        const QString name = index.data(ClipboardBoardModel::NameRole).toString();
        if (name.isEmpty() || cardPixmapCache_.contains(name)) {
            continue;
        }

        CardData card;
        card.contentType = static_cast<ContentType>(index.data(ClipboardBoardModel::ContentTypeRole).toInt());
        card.previewKind = static_cast<ClipboardPreviewKind>(index.data(ClipboardBoardModel::PreviewKindRole).toInt());
        card.previewState = static_cast<ClipboardBoardModel::PreviewState>(index.data(ClipboardBoardModel::PreviewStateRole).toInt());
        card.icon = qvariant_cast<QPixmap>(index.data(ClipboardBoardModel::IconRole));
        card.thumbnail = qvariant_cast<QPixmap>(index.data(ClipboardBoardModel::ThumbnailRole));
        card.favicon = qvariant_cast<QPixmap>(index.data(ClipboardBoardModel::FaviconRole));
        card.title = index.data(ClipboardBoardModel::TitleRole).toString();
        card.url = index.data(ClipboardBoardModel::UrlRole).toString();
        card.alias = index.data(ClipboardBoardModel::AliasRole).toString();
        card.sourceApp = index.data(ClipboardBoardModel::SourceAppRole).toString();
        card.pinned = index.data(ClipboardBoardModel::PinnedRole).toBool();
        card.time = index.data(ClipboardBoardModel::TimeRole).toDateTime();
        card.imageSize = index.data(ClipboardBoardModel::ImageSizeRole).toSize();
        card.color = qvariant_cast<QColor>(index.data(ClipboardBoardModel::ColorRole));
        card.favorite = index.data(ClipboardBoardModel::FavoriteRole).toBool();
        card.name = name;
        card.normalizedText = index.data(ClipboardBoardModel::NormalizedTextRole).toString();
        card.textLength = index.data(ClipboardBoardModel::TextLengthRole).toInt();
        card.normalizedUrls = qvariant_cast<QList<QUrl>>(index.data(ClipboardBoardModel::NormalizedUrlsRole));

        const QSize pixelSize = outerSize * paintDpr;
        QPixmap cardPixmap(pixelSize);
        cardPixmap.fill(Qt::transparent);
        cardPixmap.setDevicePixelRatio(paintDpr);
        {
            QPainter offscreen(&cardPixmap);
            offscreen.setRenderHint(QPainter::Antialiasing, true);
            offscreen.setRenderHint(QPainter::SmoothPixmapTransform, true);

            QStyleOptionViewItem offscreenOption = baseOption;
            offscreenOption.rect = QRect(QPoint(0, 0), outerSize);

            paintCardContent(&offscreen, offscreenOption, index, card, scale);
        }

        cardPixmapCache_.insert(name, new QPixmap(cardPixmap), 1);
    }
}

QColor ClipboardCardDelegate::headerColorForIcon(const QPixmap &iconPixmap) const {
    if (iconPixmap.isNull()) {
        return QColor(QStringLiteral("#4A5F7A"));
    }

    const quint64 cacheKey = static_cast<quint64>(iconPixmap.cacheKey());
    const auto it = headerColorCache_.constFind(cacheKey);
    if (it != headerColorCache_.cend()) {
        return it.value();
    }

    const QColor color = computeDominantHeaderColor(iconPixmap);
    headerColorCache_.insert(cacheKey, color);
    return color;
}

QPixmap ClipboardCardDelegate::headerIconForPixmap(const QPixmap &sourcePixmap,
                                                   const QSize &targetLogicalSize,
                                                   qreal targetDpr) const {
    QPixmap icon = sourcePixmap;
    if (icon.isNull()) {
        icon = QPixmap(QStringLiteral(":/resources/resources/unknown.svg"));
    }

    const QString cacheKey = scaledPixmapCacheKey(icon, targetLogicalSize, targetDpr);
    if (QPixmap *cached = headerIconCache_.object(cacheKey)) {
        return *cached;
    }

    QPixmap result = buildHeaderIconPixmapUncached(icon, targetLogicalSize, targetDpr);
    if (!result.isNull()) {
        headerIconCache_.insert(cacheKey, new QPixmap(result), 1);
    }
    return result;
}

QPixmap ClipboardCardDelegate::linkFallbackPreview(const QUrl &url,
                                                   const QString &title,
                                                   const QSize &targetSize,
                                                   qreal devicePixelRatio,
                                                   const QPixmap &favicon) const {
    const QString cacheKey = linkFallbackCacheKey(url, title, targetSize, devicePixelRatio, favicon);
    if (QPixmap *cached = linkFallbackCache_.object(cacheKey)) {
        return *cached;
    }

    QPixmap result = buildLinkFallbackPreviewUncached(url, title, targetSize, devicePixelRatio, favicon);
    if (!result.isNull()) {
        linkFallbackCache_.insert(cacheKey, new QPixmap(result), 1);
    }
    return result;
}

void ClipboardCardDelegate::scheduleViewportUpdate(const QPersistentModelIndex &index) const {
    auto *view = qobject_cast<QAbstractItemView *>(parent());
    if (!view || !view->viewport()) {
        return;
    }

    if (index.isValid()) {
        const QRect rect = view->visualRect(index);
        if (rect.isValid()) {
            view->viewport()->update(rect.adjusted(-2, -2, 2, 2));
            return;
        }
    }

    view->viewport()->update();
}

void ClipboardCardDelegate::ensureFileIconTimer() const {
    if (fileIconLoadTimer_) {
        return;
    }

    auto *self = const_cast<ClipboardCardDelegate *>(this);
    self->fileIconLoadTimer_ = new QTimer(self);
    self->fileIconLoadTimer_->setSingleShot(true);
    connect(self->fileIconLoadTimer_, &QTimer::timeout, self, [self]() {
        if (self->pendingFileIconRequests_.isEmpty()) {
            return;
        }

        const FileIconRequest request = self->pendingFileIconRequests_.dequeue();
        self->pendingLocalFileIconKeys_.remove(request.cacheKey);

        const QPixmap pixmap = loadLocalFileIconSync(request.filePath, request.targetLogicalSize, request.targetDpr);
        if (!pixmap.isNull()) {
            self->localFileIconCache_.insert(request.cacheKey, new QPixmap(pixmap), 1);
            self->failedLocalFileIconKeys_.remove(request.cacheKey);
        } else {
            self->failedLocalFileIconKeys_.insert(request.cacheKey);
        }

        // Invalidate cardPixmapCache so the card re-renders with the icon.
        if (request.index.isValid()) {
            const QString name = request.index.data(ClipboardBoardModel::NameRole).toString();
            if (!name.isEmpty()) {
                self->cardPixmapCache_.remove(name);
            }
        }
        self->scheduleViewportUpdate(request.index);
        if (!self->pendingFileIconRequests_.isEmpty()) {
            self->fileIconLoadTimer_->start(0);
        }
    });
}

void ClipboardCardDelegate::enqueueFileIconRequest(const FileIconRequest &request) const {
    ensureFileIconTimer();
    pendingFileIconRequests_.enqueue(request);
    if (fileIconLoadTimer_ && !fileIconLoadTimer_->isActive()) {
        fileIconLoadTimer_->start(0);
    }
}

QPixmap ClipboardCardDelegate::localImageThumbnail(const QString &filePath,
                                                   const QSize &targetLogicalSize,
                                                   qreal targetDpr,
                                                   const QModelIndex &index) const {
    if (!isLikelyImageFile(filePath) || !targetLogicalSize.isValid()) {
        return {};
    }

    const QFileInfo info(filePath);
    const QString cacheKey = filePreviewCacheKey(filePath, info, targetLogicalSize, targetDpr);
    if (QPixmap *cached = localImageThumbnailCache_.object(cacheKey)) {
        return *cached;
    }
    if (failedLocalImageThumbnailKeys_.contains(cacheKey) || pendingLocalImageThumbnailKeys_.contains(cacheKey)) {
        return {};
    }

    pendingLocalImageThumbnailKeys_.insert(cacheKey);
    const QPersistentModelIndex persistentIndex(index);
    QPointer<ClipboardCardDelegate> guard(const_cast<ClipboardCardDelegate *>(this));
    previewTaskPool_.start(QRunnable::create([guard, cacheKey, filePath, targetLogicalSize, targetDpr, persistentIndex]() {
        const QImage image = loadLocalImageThumbnailImageSync(filePath, targetLogicalSize, targetDpr);
        if (!guard) {
            return;
        }

        QMetaObject::invokeMethod(guard.data(), [guard, cacheKey, image, targetDpr, persistentIndex]() {
            if (!guard) {
                return;
            }

            guard->pendingLocalImageThumbnailKeys_.remove(cacheKey);
            if (image.isNull()) {
                guard->failedLocalImageThumbnailKeys_.insert(cacheKey);
            } else {
                QPixmap pixmap = QPixmap::fromImage(image);
                pixmap.setDevicePixelRatio(qMax<qreal>(1.0, targetDpr));
                guard->localImageThumbnailCache_.insert(cacheKey, new QPixmap(pixmap), 1);
                guard->failedLocalImageThumbnailKeys_.remove(cacheKey);
            }
            // Invalidate cardPixmapCache so the card re-renders with the thumbnail.
            if (persistentIndex.isValid()) {
                const QString name = persistentIndex.data(ClipboardBoardModel::NameRole).toString();
                if (!name.isEmpty()) {
                    guard->cardPixmapCache_.remove(name);
                }
            }
            guard->scheduleViewportUpdate(persistentIndex);
        }, Qt::QueuedConnection);
    }));
    return {};
}

QPixmap ClipboardCardDelegate::localFileIcon(const QString &filePath,
                                             const QSize &targetLogicalSize,
                                             qreal targetDpr,
                                             const QModelIndex &index) const {
    if (filePath.isEmpty() || !targetLogicalSize.isValid()) {
        return {};
    }

    const QFileInfo info(filePath);
    const QString cacheKey = filePreviewCacheKey(filePath, info, targetLogicalSize, targetDpr);
    if (QPixmap *cached = localFileIconCache_.object(cacheKey)) {
        return *cached;
    }
    if (failedLocalFileIconKeys_.contains(cacheKey) || pendingLocalFileIconKeys_.contains(cacheKey)) {
        return {};
    }

    pendingLocalFileIconKeys_.insert(cacheKey);
    enqueueFileIconRequest({cacheKey, filePath, targetLogicalSize, targetDpr, QPersistentModelIndex(index)});
    return {};
}

void ClipboardCardDelegate::drawSelectionBorder(QPainter *painter, const QStyleOptionViewItem &option,
                                                bool selected, int scale) const {
    if (!selected) {
        return;
    }
    const QRect cardRect(option.rect.topLeft() + cardOriginForScale(scale),
                         QSize(kCardBaseWidth * scale / 100, kCardBaseHeight * scale / 100));
    // A ring outside the card with a gap, concentric with the card's corners.
    const qreal cardRadius = cardRadiusForScale(scale);
    const qreal margin = cardSelectionMarginForScale(scale);
    const qreal borderWidth = qMax(2, 2 * scale / 100);
    const qreal offset = margin - borderWidth / 2.0;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(borderColor_, borderWidth));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(QRectF(cardRect).adjusted(-offset, -offset, offset, offset),
                             cardRadius + offset, cardRadius + offset);
    painter->restore();
}

void ClipboardCardDelegate::setHoverGlow(const QPersistentModelIndex &entering, qreal enteringLevel,
                                         const QPersistentModelIndex &leaving, qreal leavingLevel) {
    glowEntering_ = entering;
    glowEnteringLevel_ = enteringLevel;
    glowLeaving_ = leaving;
    glowLeavingLevel_ = leavingLevel;
}

qreal ClipboardCardDelegate::hoverGlowLevel(const QModelIndex &index) const {
    if (!index.isValid()) {
        return 0.0;
    }
    if (index == glowEntering_) {
        return glowEnteringLevel_;
    }
    if (index == glowLeaving_) {
        return glowLeavingLevel_;
    }
    return 0.0;
}

void ClipboardCardDelegate::drawHoverGlow(QPainter *painter, const QStyleOptionViewItem &option,
                                          qreal level, int scale) const {
    if (level <= 0.0) {
        return;
    }
    const QRect cardRect(option.rect.topLeft() + cardOriginForScale(scale),
                         QSize(kCardBaseWidth * scale / 100, kCardBaseHeight * scale / 100));
    const qreal radius = cardRadiusForScale(scale);
    const bool dark = ThemeManager::instance()->isDark();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    // A lift of the whole card, and its edge catching a little more light.
    painter->setPen(QPen(QColor(255, 255, 255, qRound((dark ? 34 : 120) * level)), 1));
    painter->setBrush(QColor(255, 255, 255, qRound((dark ? 14 : 30) * level)));
    painter->drawRoundedRect(QRectF(cardRect).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter->restore();
}

void ClipboardCardDelegate::drawShortcutOverlay(QPainter *painter, const QStyleOptionViewItem &option,
                                                const QString &shortcutText, int scale) const {
    if (shortcutText.isEmpty()) {
        return;
    }
    const QRect cardRect(option.rect.topLeft() + cardOriginForScale(scale),
                         QSize(kCardBaseWidth * scale / 100, kCardBaseHeight * scale / 100));
    const int footerHeight = kCardFooterHeight * scale / 100;
    const QRect footerRect(cardRect.left(), cardRect.bottom() - footerHeight + 1, cardRect.width(), footerHeight);
    const int footerPadding = qMax(6, 8 * scale / 100);

    QFont footerFont = painter->font();
    CardRenderUtils::applyUiFontDefaults(footerFont);
    footerFont.setPointSize(qMax(8, 8 * scale / 100));
    const QFontMetrics fm(footerFont);
    const int shortcutPadding = qMax(6, 8 * scale / 100);
    const int shortcutMinWidth = qMax(48, 56 * scale / 100);
    const int shortcutWidth = qMax(shortcutMinWidth, fm.horizontalAdvance(shortcutText) + shortcutPadding);
    const QRect shortcutRect(
        footerRect.right() - footerPadding - shortcutWidth + 1,
        footerRect.top(),
        shortcutWidth,
        footerRect.height());

    const CardTheme &theme = cachedTheme_;
    const QColor bgColor = theme.shortcutBgColor;
    const QColor textColor = theme.shortcutTextColor;
    const qreal pillRadius = qMax(4.0, 5.0 * scale / 100);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bgColor);
    painter->drawRoundedRect(QRectF(shortcutRect).adjusted(1, 2, -1, -2), pillRadius, pillRadius);
    painter->restore();
    CardRenderUtils::drawElidedText(painter, shortcutRect, shortcutText, footerFont, textColor,
                   Qt::AlignCenter);
}

// INVARIANT: paint() must NEVER trigger disk I/O.  All data comes from
// the model (in-memory) or the cardPixmapCache_.  Thumbnails and MIME
// payloads are loaded asynchronously elsewhere; paint() only reads
// what is already available.
void ClipboardCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &itemOption, const QModelIndex &index) const {
    if (!painter || !index.isValid()) {
        return;
    }
    // A card that opens a new day sits after a wider gap holding the day's divider.
    QStyleOptionViewItem cardOption = itemOption;
    cardOption.rect.setLeft(itemOption.rect.left() + dayGroupLead(index, MPasteSettings::getInst()->getItemScale()));

    // --- Fast path: check card pixmap cache before fetching model data ---
    const QString name = index.data(ClipboardBoardModel::NameRole).toString();
    if (name.isEmpty()) {
        return;
    }
    const bool isSelected = cardOption.state & QStyle::State_Selected;
    const QString shortcutText = index.data(ClipboardBoardModel::ShortcutTextRole).toString();
    const int previewState = index.data(ClipboardBoardModel::PreviewStateRole).toInt();
    const qreal paintDpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize outerSize = cardOuterSizeForScale(scale);

    // Cache key is name only — selection border and shortcut text are
    // drawn as overlays after blitting the cached card.
    const QString &cardCacheKey = name;

    if (const QPixmap *cached = cardPixmapCache_.object(cardCacheKey)) {
        painter->drawPixmap(cardOption.rect.topLeft(), *cached);
        drawHoverGlow(painter, cardOption, hoverGlowLevel(index), scale);
        drawSelectionBorder(painter, cardOption, isSelected, scale);
        drawShortcutOverlay(painter, cardOption, shortcutText, scale);
        return;
    }

    // --- Cache miss: fetch full card data and render to pixmap ---
    CardData card;
    card.contentType = static_cast<ContentType>(index.data(ClipboardBoardModel::ContentTypeRole).toInt());
    card.previewKind = static_cast<ClipboardPreviewKind>(index.data(ClipboardBoardModel::PreviewKindRole).toInt());
    card.previewState = static_cast<ClipboardBoardModel::PreviewState>(previewState);
    card.icon = qvariant_cast<QPixmap>(index.data(ClipboardBoardModel::IconRole));
    card.thumbnail = qvariant_cast<QPixmap>(index.data(ClipboardBoardModel::ThumbnailRole));
    card.favicon = qvariant_cast<QPixmap>(index.data(ClipboardBoardModel::FaviconRole));
    card.title = index.data(ClipboardBoardModel::TitleRole).toString();
    card.url = index.data(ClipboardBoardModel::UrlRole).toString();
    card.alias = index.data(ClipboardBoardModel::AliasRole).toString();
    card.sourceApp = index.data(ClipboardBoardModel::SourceAppRole).toString();
    card.pinned = index.data(ClipboardBoardModel::PinnedRole).toBool();
    card.time = index.data(ClipboardBoardModel::TimeRole).toDateTime();
    card.imageSize = index.data(ClipboardBoardModel::ImageSizeRole).toSize();
    card.color = qvariant_cast<QColor>(index.data(ClipboardBoardModel::ColorRole));
    card.favorite = index.data(ClipboardBoardModel::FavoriteRole).toBool();
    card.shortcutText = shortcutText;
    card.name = name;

    card.normalizedText = index.data(ClipboardBoardModel::NormalizedTextRole).toString();
    card.textLength = index.data(ClipboardBoardModel::TextLengthRole).toInt();
    card.normalizedUrls = qvariant_cast<QList<QUrl>>(index.data(ClipboardBoardModel::NormalizedUrlsRole));

    // Render the card to an offscreen pixmap, cache it, then blit.
    const QSize pixelSize = outerSize * paintDpr;
    QPixmap cardPixmap(pixelSize);
    cardPixmap.fill(Qt::transparent);
    cardPixmap.setDevicePixelRatio(paintDpr);
    {
        QPainter offscreen(&cardPixmap);
        offscreen.setRenderHints(painter->renderHints());

        QStyleOptionViewItem offscreenOption = cardOption;
        offscreenOption.rect = QRect(QPoint(0, 0), outerSize);

        paintCardContent(&offscreen, offscreenOption, index, card, scale);
    }

    cardPixmapCache_.insert(cardCacheKey, new QPixmap(cardPixmap), 1);
    painter->drawPixmap(cardOption.rect.topLeft(), cardPixmap);
    drawHoverGlow(painter, cardOption, hoverGlowLevel(index), scale);
    drawSelectionBorder(painter, cardOption, isSelected, scale);
    drawShortcutOverlay(painter, cardOption, shortcutText, scale);
}

void ClipboardCardDelegate::paintCardContent(QPainter *painter, const QStyleOptionViewItem &option,
                                              const QModelIndex &index, const CardData &card, int scale) const {
    // Two looks share one card: Still (a neutral card, the content first and a
    // single quiet line of who and when) and Aurora (the app's colour as light
    // in the corner, a header with its icon). Neither paints a colour band.
    const bool aurora = MPasteSettings::getInst()->getCardStyle() == MPasteSettings::CardAurora;
    auto px = [scale](int value) { return qMax(1, value * scale / 100); };
    const QSize innerSize(kCardBaseWidth * scale / 100, kCardBaseHeight * scale / 100);
    const QRect cardRect(option.rect.topLeft() + cardOriginForScale(scale), innerSize);
    const qreal cardRadius = aurora ? qMax(11, 16 * scale / 100) : cardRadiusForScale(scale);
    const CardTheme &theme = cachedTheme_;
    const bool darkTheme = ThemeManager::instance()->isDark();
    const qreal paintDpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;

    const QColor appColor = headerColorForIcon(card.icon);
    // The app's hue at a set strength: a tile that carries white, and a light.
    const QColor appTile = cardToneForColor(appColor, false);
    const QColor appHsl = appTile.toHsl();
    QColor appLight = QColor::fromHslF(qMax<qreal>(0.0, appHsl.hslHueF()), qMin<qreal>(appHsl.hslSaturationF() * 1.2, 0.72), 0.55);

    // --- words ---
    const QString typeLabel = typeLabelForCard(card);
    const bool isCode = (card.contentType == Text || card.contentType == RichText)
        && CardRenderUtils::looksLikeCode(card.normalizedText);
    QString kindText = isCode ? QObject::tr("Code") : typeLabel;
    if (card.contentType == RichText && MxGraphRenderer::isMxGraphContent(card.normalizedText)) {
        kindText = QStringLiteral("draw.io");
    } else if (!card.title.isEmpty() && card.contentType == Office) {
        kindText += QStringLiteral(" · ") + card.title;
    }
    const QString countLabel = countLabelForCard(card);
    const QString alias = card.alias.trimmed();
    const QString title = !alias.isEmpty() ? alias : (!card.sourceApp.isEmpty() ? card.sourceApp : typeLabel);
    // When the title already names the kind (no app known), the detail line does not repeat it.
    const bool titleIsKind = title == typeLabel && kindText == typeLabel;
    QString detailText = countLabel.isEmpty() ? kindText
        : (titleIsKind ? countLabel : kindText + QStringLiteral(" · ") + countLabel);
    if (isOcrPending(card.name)) {
        detailText += QStringLiteral(" · OCR...");
    }
    const QString when = compactTimeLabel(card.time);
    const bool middleElide = card.contentType == Link || (card.contentType == File && card.normalizedUrls.size() == 1);
    // Pictures sit in the card as pictures: framed in Still, edge to edge in Aurora.
    const bool visual = card.contentType == Image || card.contentType == Office;

    const QColor ink = darkTheme ? QColor(232, 233, 238, 190) : QColor(31, 33, 41, 200);
    const QColor muted = theme.footerTextColor;

    auto uiFont = [&](qreal points, QFont::Weight weight) {
        QFont font = painter->font();
        CardRenderUtils::applyUiFontDefaults(font);
        font.setPointSizeF(points * scale / 100);
        font.setWeight(weight);
        return font;
    };
    // The source app's icon, or a tile in its colour with the first character.
    auto drawAppIcon = [&](const QRect &rect, qreal radius) {
        if (!isPlaceholderIcon(card.icon)) {
            painter->drawPixmap(rect, headerIconForPixmap(card.icon, rect.size(), paintDpr));
            return;
        }
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(appTile);
        painter->drawRoundedRect(QRectF(rect), radius, radius);
        QFont glyph = painter->font();
        CardRenderUtils::applyUiFontDefaults(glyph);
        glyph.setWeight(QFont::DemiBold);
        glyph.setPixelSize(qMax(7, int(rect.height() * 0.52)));
        painter->setFont(glyph);
        painter->setPen(Qt::white);
        painter->drawText(rect, Qt::AlignCenter, (card.sourceApp.isEmpty() ? typeLabel : card.sourceApp).left(1).toUpper());
        painter->restore();
    };
    // Favourite and pinned marks, right to left from `right`; returns the new right edge.
    auto drawMarks = [&](int right, int centerY, int size) {
        if (card.favorite) {
            const QRect starRect(right - size + 1, centerY - size / 2, size, size);
            painter->drawPixmap(starRect, starPixmap(starRect.size()));
            right = starRect.left() - px(6);
        }
        if (card.pinned) {
            const QRect pinRect(right - size + 1, centerY - size / 2, size, size);
            painter->drawPixmap(pinRect, pinPixmap(pinRect.size(), darkTheme));
            right = pinRect.left() - px(6);
        }
        return right;
    };

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    drawShadow(painter, QRectF(cardRect), cardRadius);

    QPainterPath cardPath;
    cardPath.addRoundedRect(QRectF(cardRect), cardRadius, cardRadius);

    // --- surface ---
    painter->save();
    painter->setClipPath(cardPath);
    painter->setPen(Qt::NoPen);
    if (aurora) {
        const bool deep = visual && darkTheme;
        painter->fillPath(cardPath, deep ? QColor(QStringLiteral("#0D0E12"))
                                         : (darkTheme ? QColor(QStringLiteral("#17181E")) : QColor(QStringLiteral("#FCFCFE"))));
        {
            // The app's colour as light from the top-left corner, not a painted band:
            // an ellipse wide across the top and shallow down the card (130% of its
            // width by 90% of its height, gone at 62%), so it stays near the header.
            // On a picture it lights the header above it.
            const qreal radiusX = cardRect.width() * 1.3;
            const qreal radiusY = cardRect.height() * 0.9;
            QRadialGradient light(QPointF(0, 0), radiusX);
            QColor glow = appLight;
            glow.setAlpha(darkTheme ? 92 : 58);
            light.setColorAt(0.0, glow);
            glow.setAlpha(0);
            light.setColorAt(0.62, glow);
            QBrush brush(light);
            QTransform place;
            place.translate(cardRect.left(), cardRect.top());
            place.scale(1.0, radiusY / radiusX);
            brush.setTransform(place);
            painter->fillRect(cardRect, brush);
        }
    } else {
        painter->fillPath(cardPath, darkTheme ? QColor(QStringLiteral("#18191D")) : QColor(Qt::white));
    }
    painter->restore();

    // A hairline edge, a little brighter along the top as if lit from above.
    {
        const QRectF edgeRect = QRectF(cardRect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(darkTheme ? QColor(255, 255, 255, aurora ? 18 : 15) : QColor(0, 0, 0, 18), 1.0));
        painter->drawRoundedRect(edgeRect, cardRadius, cardRadius);
        QLinearGradient rim(edgeRect.topLeft(), QPointF(edgeRect.left(), edgeRect.top() + px(40)));
        rim.setColorAt(0.0, QColor(255, 255, 255, darkTheme ? (aurora ? 34 : 22) : 120));
        rim.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter->setPen(QPen(QBrush(rim), 1.0));
        painter->drawRoundedRect(edgeRect, cardRadius, cardRadius);
    }

    QRect bodyRect;
    const int pad = px(14);
    if (aurora) {
        const int headerHeight = px(38);
        const int footerHeight = px(30);
        const QRect header(cardRect.left(), cardRect.top(), cardRect.width(), headerHeight);
        const QRect footer(cardRect.left(), cardRect.bottom() - footerHeight + 1, cardRect.width(), footerHeight);
        const int iconSize = px(22);
        const QRect iconRect(header.left() + pad, header.center().y() - iconSize / 2 + 1, iconSize, iconSize);
        drawAppIcon(iconRect, px(6));
        const QFont timeFont = uiFont(8.5, QFont::Normal);
        const int timeWidth = QFontMetrics(timeFont, painter->device()).horizontalAdvance(when) + px(4);
        const QRect timeRect(header.right() - pad - timeWidth + 1, header.top(), timeWidth, headerHeight);
        const int titleRight = drawMarks(timeRect.left() - px(8), header.center().y() + 1, px(13));
        const int titleLeft = iconRect.right() + 1 + px(10);
        const QColor headInk = visual && darkTheme ? QColor(255, 255, 255, 235)
                                                   : (darkTheme ? QColor(255, 255, 255, 235) : QColor(24, 26, 32, 235));
        CardRenderUtils::drawElidedText(painter, QRect(titleLeft, header.top(), qMax(0, titleRight - titleLeft), headerHeight),
                                        title, uiFont(9.75, QFont::DemiBold), headInk, Qt::AlignLeft | Qt::AlignVCenter);
        QColor headSoft = headInk;
        headSoft.setAlpha(140);
        CardRenderUtils::drawElidedText(painter, timeRect, when, timeFont, headSoft, Qt::AlignRight | Qt::AlignVCenter);

        // Text keeps the icon's edge on the left and the same margin on the right.
        const QRect content(cardRect.left() + pad, header.bottom() + px(2), cardRect.width() - pad * 2,
                            footer.top() - header.bottom() - px(4));
        if (visual) {
            bodyRect = QRect(cardRect.left(), header.bottom() + 1, cardRect.width(), cardRect.bottom() - header.bottom());
        } else {
            // The renderers keep their own 12/8 px inset; start them so the text lands on the content edge.
            bodyRect = content.adjusted(-px(12), -px(8), px(12), px(4));
        }
        painter->save();
        painter->setClipPath(cardPath);
        const CardBodyContext ctx {bodyRect, visual ? bodyRect : content, card, scale, darkTheme, loadingPhase_, paintDpr,
                                   theme.bodyTextColor, index, this, cachedTheme_};
        bodyRendererForType(card.contentType).paint(painter, ctx);
        QColor footInk = muted;
        if (visual) {
            // The caption rests on a shade over the picture's foot.
            QLinearGradient shade(QPointF(footer.left(), footer.top() - px(10)), QPointF(footer.left(), footer.bottom()));
            const QColor base = darkTheme ? QColor(13, 14, 18) : QColor(252, 252, 254);
            QColor clear = base;
            clear.setAlpha(0);
            QColor solid = base;
            solid.setAlpha(225);
            shade.setColorAt(0.0, clear);
            shade.setColorAt(1.0, solid);
            painter->fillRect(footer.adjusted(0, -px(10), 0, 0), shade);
            footInk = darkTheme ? QColor(255, 255, 255, 190) : QColor(31, 33, 41, 190);
        }
        painter->restore();
        CardRenderUtils::drawElidedText(painter, footer.adjusted(pad + px(2), 0, -pad, 0), detailText,
                                        uiFont(8.5, QFont::Normal), footInk, Qt::AlignLeft | Qt::AlignVCenter,
                                        middleElide ? Qt::ElideMiddle : Qt::ElideRight);
    } else {
        // Still: the content takes the card; one line at the foot says from where and when.
        const int metaHeight = px(18);
        const QRect meta(cardRect.left() + pad, cardRect.bottom() - px(12) - metaHeight + 1,
                         cardRect.width() - pad * 2, metaHeight);
        const QRect content(cardRect.left() + pad, cardRect.top() + pad, cardRect.width() - pad * 2,
                            meta.top() - px(10) - cardRect.top() - pad);
        const int iconSize = px(16);
        const QRect iconRect(meta.left(), meta.center().y() - iconSize / 2 + 1, iconSize, iconSize);
        drawAppIcon(iconRect, px(5));

        const QFont metaFont = uiFont(8.5, QFont::Normal);
        const QFontMetrics metaMetrics(metaFont, painter->device());
        // The right side says what it is; the left who and when, and gives way first.
        const int detailWidth = qMin(metaMetrics.horizontalAdvance(detailText) + px(2), meta.width() / 2);
        const QRect detailRect(meta.right() - detailWidth + 1, meta.top(), detailWidth, metaHeight);
        CardRenderUtils::drawElidedText(painter, detailRect, detailText, metaFont, muted,
                                        Qt::AlignRight | Qt::AlignVCenter, middleElide ? Qt::ElideMiddle : Qt::ElideRight);
        const int marksRight = drawMarks(detailRect.left() - px(8), meta.center().y() + 1, px(12));
        const int left = iconRect.right() + 1 + px(7);
        const QFont nameFont = uiFont(8.5, QFont::Medium);
        const int timeWidth = metaMetrics.horizontalAdvance(when) + px(2);
        const int nameWidth = qMax(0, qMin(QFontMetrics(nameFont, painter->device()).horizontalAdvance(title) + px(2),
                                           marksRight - left - timeWidth - px(6)));
        CardRenderUtils::drawElidedText(painter, QRect(left, meta.top(), nameWidth, metaHeight), title, nameFont, ink,
                                        Qt::AlignLeft | Qt::AlignVCenter);
        CardRenderUtils::drawElidedText(painter, QRect(left + nameWidth + px(6), meta.top(), timeWidth, metaHeight), when,
                                        metaFont, muted, Qt::AlignLeft | Qt::AlignVCenter);

        painter->save();
        if (visual) {
            // A picture sits in a frame of its own inside the card.
            QPainterPath frame;
            frame.addRoundedRect(QRectF(content), px(9), px(9));
            painter->setClipPath(frame);
            painter->fillPath(frame, darkTheme ? QColor(12, 13, 15) : QColor(242, 243, 246));
            bodyRect = content;
        } else {
            painter->setClipPath(cardPath);
            // The renderers keep their own 12/8 px inset; start them so the text lands on the content edge.
            bodyRect = content.adjusted(-px(12), -px(8), px(12), px(4));
        }
        const CardBodyContext ctx {bodyRect, content, card, scale, darkTheme, loadingPhase_, paintDpr,
                                   theme.bodyTextColor, index, this, cachedTheme_};
        bodyRendererForType(card.contentType).paint(painter, ctx);
        painter->restore();
    }

    painter->restore();
}

QSize ClipboardCardDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(option)
    const int scale = MPasteSettings::getInst()->getItemScale();
    return cardOuterSizeForScale(scale) + QSize(cardSpacingForScale(scale) + dayGroupLead(index, scale), 0);
}

QDate ClipboardCardDelegate::dayGroup(const QModelIndex &index) {
    if (!index.isValid() || index.data(ClipboardBoardModel::PinnedRole).toBool()) {
        return {};   // pinned items lead the list as their own, unmarked group
    }
    return index.data(ClipboardBoardModel::TimeRole).toDateTime().date();
}

int ClipboardCardDelegate::dayGroupLead(const QModelIndex &index, int scale) {
    if (!index.isValid() || index.row() == 0) {
        return 0;
    }
    const QDate day = dayGroup(index);
    if (!day.isValid() || day == dayGroup(index.sibling(index.row() - 1, index.column()))) {
        return 0;
    }
    return qMax(16, 26 * scale / 100);
}
