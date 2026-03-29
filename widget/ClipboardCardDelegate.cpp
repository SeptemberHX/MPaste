// input: Depends on ClipboardCardDelegate.h, card metrics, and ClipboardItem display data.
// output: Implements the manual card painter for the delegate-based clipboard board.
// pos: Widget-layer delegate implementation that replaces per-item QWidget rendering.
// update: If I change, update this header block and my folder README.md (smaller card typography + file image thumbnails + improved file previews + footer height tweak + link preview caption trimmed + header spacing + link url shortcut spacing + custom alias header line + pinned badge + rich text fill + data-layer preview kind + async file preview caching).
// note: Adjusted card palette for dark theme and moved heavy file preview work out of paint.
#include "ClipboardCardDelegate.h"

#include <cmath>

#include <QAbstractItemView>
#include <QApplication>
#include <QCache>
#include <QDebug>
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

QString typeLabelForCard(const CardData &card) {
    switch (card.contentType) {
        case Text:
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
                : QStringLiteral("1 %1").arg(QObject::tr("File"));
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
                ? QStringLiteral("%1 x %2 %3").arg(card.imageSize.width()).arg(card.imageSize.height()).arg(QObject::tr("Pixels"))
                : QString();
        case RichText:
        case Text:
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
                    return QFileInfo(url.toLocalFile()).absolutePath();
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
    headlineFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
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
    QPixmap pixmap = icon.pixmap(pixelTargetSize);
    if (pixmap.isNull()) {
        return {};
    }
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

void ClipboardCardDelegate::preRenderAll(QAbstractItemModel *model, const QStyleOptionViewItem &baseOption) {
    if (!model) {
        return;
    }
    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize outerSize = cardOuterSizeForScale(scale);
    const qreal paintDpr = qMax(1.0, static_cast<qreal>(baseOption.decorationSize.width()));

    for (int row = 0; row < model->rowCount(); ++row) {
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
    const QRect cardRect(option.rect.topLeft(),
                         QSize(kCardBaseWidth * scale / 100, kCardBaseHeight * scale / 100));
    const int cardRadius = qMax(8, 12 * scale / 100);
    const int borderWidth = qMax(2, 3 * scale / 100);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(borderColor_, borderWidth));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(QRectF(cardRect).adjusted(0.5, 0.5, -0.5, -0.5), cardRadius, cardRadius);
    painter->restore();
}

void ClipboardCardDelegate::drawShortcutOverlay(QPainter *painter, const QStyleOptionViewItem &option,
                                                const QString &shortcutText, int scale) const {
    if (shortcutText.isEmpty()) {
        return;
    }
    const QRect cardRect(option.rect.topLeft(),
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

void ClipboardCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    if (!painter || !index.isValid()) {
        return;
    }

    // --- Fast path: check card pixmap cache before fetching model data ---
    const QString name = index.data(ClipboardBoardModel::NameRole).toString();
    if (name.isEmpty()) {
        return;
    }
    const bool isSelected = option.state & QStyle::State_Selected;
    const QString shortcutText = index.data(ClipboardBoardModel::ShortcutTextRole).toString();
    const int previewState = index.data(ClipboardBoardModel::PreviewStateRole).toInt();
    const qreal paintDpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize outerSize = cardOuterSizeForScale(scale);

    // Cache key is name only — selection border and shortcut text are
    // drawn as overlays after blitting the cached card.
    const QString &cardCacheKey = name;

    if (const QPixmap *cached = cardPixmapCache_.object(cardCacheKey)) {
        painter->drawPixmap(option.rect.topLeft(), *cached);
        drawSelectionBorder(painter, option, isSelected, scale);
        drawShortcutOverlay(painter, option, shortcutText, scale);
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

        QStyleOptionViewItem offscreenOption = option;
        offscreenOption.rect = QRect(QPoint(0, 0), outerSize);

        paintCardContent(&offscreen, offscreenOption, index, card, scale);
    }

    cardPixmapCache_.insert(cardCacheKey, new QPixmap(cardPixmap), 1);
    painter->drawPixmap(option.rect.topLeft(), cardPixmap);
    drawSelectionBorder(painter, option, isSelected, scale);
    drawShortcutOverlay(painter, option, shortcutText, scale);
}

void ClipboardCardDelegate::paintCardContent(QPainter *painter, const QStyleOptionViewItem &option,
                                              const QModelIndex &index, const CardData &card, int scale) const {
    const QSize outerSize = cardOuterSizeForScale(scale);
    const QSize innerSize(kCardBaseWidth * scale / 100, kCardBaseHeight * scale / 100);
    const int topHeight = kCardHeaderHeight * scale / 100;
    const int footerHeight = kCardFooterHeight * scale / 100;
    const bool hideFooter = false;
    const int effectiveFooterHeight = hideFooter ? 0 : footerHeight;
    const int iconLabelSize = 48 * scale / 100;
    const int iconPixmapSize = 40 * scale / 100;
    const qreal cardRadius = qMax(10, 12 * scale / 100);
    const QRect outerRect(option.rect.topLeft(), outerSize);
    const QRect cardRect(outerRect.topLeft(), innerSize);
    const QRect topRect(cardRect.left(), cardRect.top(), cardRect.width(), topHeight);
    const QRect bodyRect(cardRect.left(), topRect.bottom(), cardRect.width(), cardRect.height() - topHeight - effectiveFooterHeight);
    const QRect footerRect(cardRect.left(), cardRect.bottom() - effectiveFooterHeight + 1, cardRect.width(), effectiveFooterHeight);

    const CardTheme &theme = cachedTheme_;
    const bool darkTheme = ThemeManager::instance()->isDark();
    const QColor baseSurface = theme.baseSurface;
    const QColor bodyTextColor = theme.bodyTextColor;
    const QColor footerTextColor = theme.footerTextColor;
    const QColor subtleBorderColor = theme.subtleBorderColor;
    const QColor topColor = headerColorForIcon(card.icon);
    const QColor bgColor = blendColor(topColor, baseSurface, theme.headerBlendFactor);
    // Selection border is drawn as overlay after cache blit — always
    // render the cached card in the unselected state.
    const bool selected = false;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    drawShadow(painter, QRectF(cardRect), cardRadius);

    QPainterPath cardPath;
    cardPath.addRoundedRect(QRectF(cardRect), cardRadius, cardRadius);

    if (card.favorite && !selected) {
        painter->setPen(QPen(theme.favoriteBorderColor, 5.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(cardRect).adjusted(3.0, 3.0, -3.0, -3.0), cardRadius, cardRadius);
    }

    painter->save();
    painter->setClipPath(cardPath);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bgColor);
    painter->drawRoundedRect(cardRect, cardRadius, cardRadius);

    QLinearGradient topGradient(topRect.topLeft(), topRect.topRight());
    topGradient.setColorAt(0.0, topColor.lighter(122));
    topGradient.setColorAt(1.0, topColor.darker(112));
    painter->fillRect(topRect, topGradient);
    painter->fillRect(bodyRect, bgColor);
    if (!hideFooter) {
        painter->fillRect(footerRect, bgColor);
    }
    painter->restore();

    const int topRightMargin = 10 * scale / 100;
    const int baseTextLeftMargin = 24 * scale / 100;
    const QRect iconRect(topRect.right() - topRightMargin - iconLabelSize + 1,
                         topRect.top() + (topHeight - iconLabelSize) / 2,
                         iconLabelSize, iconLabelSize);
    const QRect iconPixmapRect(iconRect.left() + (iconRect.width() - iconPixmapSize) / 2,
                               iconRect.top() + (iconRect.height() - iconPixmapSize) / 2,
                               iconPixmapSize, iconPixmapSize);
    const qreal paintDpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QPixmap headerIcon = headerIconForPixmap(card.icon, iconPixmapRect.size(), paintDpr);
    {
        // Draw a dark shadow behind the icon.
        QImage shadowImg = headerIcon.toImage();
        shadowImg.fill(QColor(0, 0, 0, 80));
        shadowImg.setDevicePixelRatio(headerIcon.devicePixelRatioF());
        // Use the original icon's alpha as mask so the shadow has the same shape.
        QPainter shadowPainter(&shadowImg);
        shadowPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        shadowPainter.drawPixmap(0, 0, headerIcon);
        shadowPainter.end();
        painter->drawImage(iconPixmapRect.topLeft() + QPoint(1, 2), shadowImg);
    }
    painter->drawPixmap(iconPixmapRect.topLeft(), headerIcon);

    QFont typeFont = painter->font();
    typeFont.setFamily(QStringLiteral("Microsoft YaHei"));
    typeFont.setPointSize(qMax(10, 12 * scale / 100));
    typeFont.setBold(true);
    typeFont.setWeight(QFont::ExtraBold);
    QFont timeFont = painter->font();
    CardRenderUtils::applyUiFontDefaults(timeFont);
    timeFont.setPointSize(qMax(8, 9 * scale / 100));

    const int textRightPadding = iconLabelSize + topRightMargin + 12 * scale / 100;
    const QFontMetrics typeMetrics(typeFont);
    const QFontMetrics timeMetrics(timeFont);
    const int typeHeight = qMax(18, typeMetrics.height());
    const int timeHeight = qMax(14, timeMetrics.height());
    const int textGap = qMax(2, 4 * scale / 100);
    const int textBlockHeight = typeHeight + textGap + timeHeight;
    const int textBlockTop = topRect.top() + qMax(0, (topRect.height() - textBlockHeight) / 2);
    int effectiveTextLeftMargin = baseTextLeftMargin;
    QRect pinRect;
    if (card.pinned) {
        const int pinSize = qMax(14, 20 * scale / 100);
        pinRect = QRect(cardRect.left() + qMax(4, 6 * scale / 100),
                        cardRect.top() + qMax(4, 6 * scale / 100),
                        pinSize,
                        pinSize);
        effectiveTextLeftMargin = qMax(baseTextLeftMargin, pinRect.right() + qMax(6, 8 * scale / 100));
    }

    const QRect typeRect(topRect.left() + effectiveTextLeftMargin,
                         textBlockTop,
                         topRect.width() - effectiveTextLeftMargin - textRightPadding,
                         typeHeight);
    const QRect timeRect(typeRect.left(),
                         typeRect.bottom() + 1 + textGap,
                         typeRect.width(),
                         timeHeight);
    if (card.pinned && pinRect.isValid()) {
        painter->drawPixmap(pinRect, pinPixmap(pinRect.size(), darkTheme));
    }
    const QString typeLabel = typeLabelForCard(card);
    QString timeLabel = QLocale::system().toString(card.time, QLocale::ShortFormat);
    if (card.contentType == RichText && MxGraphRenderer::isMxGraphContent(card.normalizedText)) {
        timeLabel += QStringLiteral(" \u00B7 draw.io");
    }
    const QString aliasLabel = card.alias.trimmed();
    if (!aliasLabel.isEmpty()) {
        CardRenderUtils::drawElidedText(painter, typeRect, aliasLabel, typeFont, QColor(Qt::white), Qt::AlignLeft | Qt::AlignVCenter);

        QFont metaTypeFont = timeFont;
        metaTypeFont.setFamilies(typeFont.families());
        metaTypeFont.setBold(true);
        metaTypeFont.setWeight(QFont::ExtraBold);

        const QFontMetrics metaTypeMetrics(metaTypeFont);
        const int metaGap = qMax(6, 8 * scale / 100);
        const int typeTextWidth = metaTypeMetrics.horizontalAdvance(typeLabel);
        const int typeDrawWidth = qMin(typeTextWidth, timeRect.width());
        const QRect typeMetaRect(timeRect.left(), timeRect.top(),
                                 typeDrawWidth, timeRect.height());
        CardRenderUtils::drawElidedText(painter, typeMetaRect, typeLabel, metaTypeFont, QColor(Qt::white), Qt::AlignLeft | Qt::AlignVCenter);

        const QString timeSuffix = QStringLiteral("- %1").arg(timeLabel);
        const int timeX = timeRect.left() + typeDrawWidth + metaGap;
        if (timeX < timeRect.right()) {
            const QRect timeMetaRect(timeX, timeRect.top(),
                                     timeRect.right() - timeX, timeRect.height());
            CardRenderUtils::drawElidedText(painter, timeMetaRect, timeSuffix, timeFont, QColor(Qt::white), Qt::AlignLeft | Qt::AlignVCenter);
        }
    } else {
        CardRenderUtils::drawElidedText(painter, typeRect, typeLabel, typeFont, QColor(Qt::white), Qt::AlignLeft | Qt::AlignVCenter);
        CardRenderUtils::drawElidedText(painter, timeRect, timeLabel,
                       timeFont, QColor(Qt::white), Qt::AlignLeft | Qt::AlignVCenter);
    }

    const QRect previewRect = bodyRect.adjusted(10 * scale / 100, 8 * scale / 100, -10 * scale / 100, -6 * scale / 100);
    // Clip body rendering to the card shape so thumbnails/text
    // don't bleed outside the rounded corners.
    painter->save();
    painter->setClipPath(cardPath);
    {
        const CardBodyContext ctx {
            bodyRect,
            previewRect,
            card,
            scale,
            darkTheme,
            loadingPhase_,
            paintDpr,
            bodyTextColor,
            index,
            this,
            cachedTheme_
        };
        bodyRendererForType(card.contentType).paint(painter, ctx);
    }
    painter->restore();

    if (!hideFooter) {
        QFont footerFont = painter->font();
        CardRenderUtils::applyUiFontDefaults(footerFont);
        footerFont.setPointSize(qMax(8, 8 * scale / 100));
        const int footerPadding = qMax(6, 8 * scale / 100);
        const QFontMetrics footerMetrics(footerFont);
        // Shortcut text is drawn as overlay after cache blit, not baked in.
        const QRect countRect(
            footerRect.left() + footerPadding,
            footerRect.top(),
            footerRect.width() - footerPadding * 2,
            footerRect.height());
        const bool useMiddleElide = card.contentType == Link
            || (card.contentType == File && card.normalizedUrls.size() == 1);
        CardRenderUtils::drawElidedText(painter, countRect, countLabelForCard(card), footerFont, footerTextColor,
                       Qt::AlignLeft | Qt::AlignVCenter,
                       useMiddleElide ? Qt::ElideMiddle : Qt::ElideRight);
    }

    if (card.favorite) {
        const int starSize = qMax(12, 16 * scale / 100);
        const QRect starRect(cardRect.right() - starSize - 8 * scale / 100,
                             cardRect.bottom() - starSize - 8 * scale / 100,
                             starSize, starSize);
        painter->drawPixmap(starRect, starPixmap(starRect.size()));
    }

    const int borderWidth = selected ? qMax(2, 3 * scale / 100) : qMax(1, scale / 100);
    const QColor borderColor = selected ? borderColor_ : (card.favorite ? QColor(QStringLiteral("#F4C542")) : subtleBorderColor);
    painter->setPen(QPen(borderColor, borderWidth));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(QRectF(cardRect).adjusted(0.5, 0.5, -0.5, -0.5), cardRadius, cardRadius);

    painter->restore();

}

QSize ClipboardCardDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(option)
    Q_UNUSED(index)
    return cardOuterSizeForScale(MPasteSettings::getInst()->getItemScale());
}
