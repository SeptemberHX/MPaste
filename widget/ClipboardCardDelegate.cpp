// input: Depends on ClipboardCardDelegate.h, card metrics, and ClipboardItem display data.
// output: Implements the manual card painter for the delegate-based clipboard board.
// pos: Widget-layer delegate implementation that replaces per-item QWidget rendering.
// update: If I change, update this header block and my folder README.md (smaller card typography + file image thumbnails + improved file previews + footer path tweaks + link preview caption trimmed + header spacing + link url shortcut spacing + custom alias header line + pinned badge).
// note: Adjusted card palette for dark theme.
#include "ClipboardCardDelegate.h"

#include <cmath>

#include <QCache>
#include <QDebug>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QSet>
#include <QTextLayout>
#include <QUrl>

#include "ClipboardBoardModel.h"
#include "CardMetrics.h"
#include "utils/MPasteSettings.h"
#include "utils/ThemeManager.h"

namespace {
constexpr float kPi = 3.14159265358979323846f;

bool isVeryTallImage(const QSize &size) {
    return size.isValid() && size.height() >= qMax(4000, size.width() * 4);
}

void logTallImageCoverEvent(const QString &name,
                            const QSize &imageSize,
                            const QSize &targetLogicalSize,
                            qreal targetDpr,
                            const QPixmap &pixmap,
                            const char *stage) {
    if (!isVeryTallImage(imageSize)) {
        return;
    }

    static QHash<QString, int> logCounts;
    const QString key = QStringLiteral("%1:%2").arg(name, QString::fromLatin1(stage));
    int &count = logCounts[key];
    if (count >= 6) {
        return;
    }
    ++count;

    const qreal sourceDpr = qMax<qreal>(1.0, pixmap.devicePixelRatio());
    const QSize sourceLogicalSize(qRound(pixmap.width() / sourceDpr),
                                  qRound(pixmap.height() / sourceDpr));
    qInfo().noquote() << QStringLiteral("[delegate-cover] stage=%1 name=%2 image=%3x%4 thumbPx=%5x%6 thumbLogical=%7x%8 thumbDpr=%9 target=%10x%11 targetDpr=%12")
        .arg(QString::fromLatin1(stage))
        .arg(name)
        .arg(imageSize.width())
        .arg(imageSize.height())
        .arg(pixmap.width())
        .arg(pixmap.height())
        .arg(sourceLogicalSize.width())
        .arg(sourceLogicalSize.height())
        .arg(sourceDpr, 0, 'f', 2)
        .arg(targetLogicalSize.width())
        .arg(targetLogicalSize.height())
        .arg(targetDpr, 0, 'f', 2);
}

struct CardData {
    ClipboardItem::ContentType contentType = ClipboardItem::All;
    QPixmap icon;
    QPixmap thumbnail;
    QPixmap favicon;
    QString title;
    QString url;
    QString alias;
    QString normalizedText;
    QList<QUrl> normalizedUrls;
    QDateTime time;
    QSize imageSize;
    QColor color;
    bool favorite = false;
    bool pinned = false;
    QString shortcutText;
    QString name;
};

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
        case ClipboardItem::Text:
            return QObject::tr("Plain Text");
        case ClipboardItem::Link:
            return QObject::tr("Link");
        case ClipboardItem::Image:
            return QObject::tr("Image");
        case ClipboardItem::Office:
            return QObject::tr("Office Shape");
        case ClipboardItem::RichText:
            return QObject::tr("Rich Text");
        case ClipboardItem::File:
            return card.normalizedUrls.size() > 1
                ? QStringLiteral("%1 %2").arg(card.normalizedUrls.size()).arg(QObject::tr("Files"))
                : QStringLiteral("1 %1").arg(QObject::tr("File"));
        case ClipboardItem::Color:
            return QObject::tr("Color");
        case ClipboardItem::All:
            break;
    }
    return QObject::tr("Item");
}

QString countLabelForCard(const CardData &card) {
    switch (card.contentType) {
        case ClipboardItem::Image:
        case ClipboardItem::Office:
            return card.imageSize.isValid()
                ? QStringLiteral("%1 x %2 %3").arg(card.imageSize.width()).arg(card.imageSize.height()).arg(QObject::tr("Pixels"))
                : QString();
        case ClipboardItem::RichText:
        case ClipboardItem::Text:
        case ClipboardItem::Link:
            return QStringLiteral("%1 %2").arg(card.normalizedText.size()).arg(QObject::tr("Characters"));
        case ClipboardItem::File:
            if (card.normalizedUrls.size() > 1) {
                return QStringLiteral("%1 %2").arg(card.normalizedUrls.size()).arg(QObject::tr("Files"));
            }
            if (card.normalizedUrls.size() == 1) {
                const QUrl url = card.normalizedUrls.front();
                return url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyDecoded);
            }
            return QString();
        case ClipboardItem::Color:
        case ClipboardItem::All:
            break;
    }
    return QString();
}

QString previewTextForCard(const CardData &card) {
    switch (card.contentType) {
        case ClipboardItem::File: {
            QStringList lines;
            for (const QUrl &url : card.normalizedUrls) {
                const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
                const QFileInfo info(path);
                lines << (info.fileName().isEmpty() ? path : info.fileName());
            }
            return lines.join(QLatin1Char('\n'));
        }
        case ClipboardItem::Link:
            if (!card.title.trimmed().isEmpty()) {
                return card.title.trimmed() + QLatin1Char('\n')
                    + (card.url.isEmpty() ? card.normalizedText.trimmed() : card.url);
            }
            return card.normalizedText.trimmed();
        case ClipboardItem::Color:
            return card.color.name(QColor::HexRgb);
        case ClipboardItem::RichText:
            return card.normalizedText.trimmed().isEmpty() ? QObject::tr("Rich text preview") : card.normalizedText.trimmed();
        case ClipboardItem::Image:
            return QObject::tr("Image preview");
        case ClipboardItem::Office:
            return QObject::tr("Office shape preview");
        case ClipboardItem::Text:
        case ClipboardItem::All:
            break;
    }
    return card.normalizedText.trimmed();
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

QColor dominantHeaderColor(const QPixmap &iconPixmap) {
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

void drawElidedText(QPainter *painter, const QRect &rect, const QString &text,
                    const QFont &font, const QColor &color, Qt::Alignment alignment,
                    Qt::TextElideMode elideMode = Qt::ElideRight) {
    if (text.isEmpty() || rect.isEmpty()) {
        return;
    }

    painter->save();
    painter->setFont(font);
    painter->setPen(color);
    const QFontMetrics metrics(font);
    painter->drawText(rect, alignment, metrics.elidedText(text, elideMode, rect.width()));
    painter->restore();
}

void drawWrappedText(QPainter *painter, const QRect &rect, const QString &text,
                     const QFont &font, const QColor &color) {
    if (text.isEmpty() || rect.isEmpty()) {
        return;
    }

    painter->save();
    painter->setFont(font);
    painter->setPen(color);
    painter->drawText(rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    painter->restore();
}

void drawCoverPixmap(QPainter *painter, const QRect &targetRect, const QPixmap &pixmap,
                     const QString &debugName = QString(), const QSize &debugImageSize = QSize()) {
    if (targetRect.isEmpty() || pixmap.isNull()) {
        return;
    }

    const qreal targetDpr = painter && painter->device()
        ? qMax<qreal>(1.0, painter->device()->devicePixelRatioF())
        : qMax<qreal>(1.0, pixmap.devicePixelRatio());
    const QSize pixelTargetSize = targetRect.size() * targetDpr;
    if (!pixelTargetSize.isValid()) {
        return;
    }

    const qreal sourceDpr = qMax<qreal>(1.0, pixmap.devicePixelRatio());
    const QSize sourceLogicalSize(qRound(pixmap.width() / sourceDpr),
                                  qRound(pixmap.height() / sourceDpr));
    if (qFuzzyCompare(sourceDpr, targetDpr) && sourceLogicalSize == targetRect.size()) {
        logTallImageCoverEvent(debugName, debugImageSize, targetRect.size(), targetDpr, pixmap, "direct");
        painter->drawPixmap(targetRect.topLeft(), pixmap);
        return;
    }

    static QCache<QString, QPixmap> cache(96 * 1024);
    const QString cacheKey = QStringLiteral("%1:%2x%3@%4")
        .arg(QString::number(static_cast<qulonglong>(pixmap.cacheKey())))
        .arg(pixelTargetSize.width())
        .arg(pixelTargetSize.height())
        .arg(qRound(targetDpr * 100.0));
    if (QPixmap *cached = cache.object(cacheKey)) {
        logTallImageCoverEvent(debugName, debugImageSize, targetRect.size(), targetDpr, *cached, "cache-hit");
        painter->drawPixmap(targetRect.topLeft(), *cached);
        return;
    }

    logTallImageCoverEvent(debugName, debugImageSize, targetRect.size(), targetDpr, pixmap, "cache-miss");
    QPixmap scaled = pixmap.scaled(pixelTargetSize,
        Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QRect cropRect(qMax(0, (scaled.width() - pixelTargetSize.width()) / 2),
                         qMax(0, (scaled.height() - pixelTargetSize.height()) / 2),
                         qMin(pixelTargetSize.width(), scaled.width()),
                         qMin(pixelTargetSize.height(), scaled.height()));
    QPixmap cropped = scaled.copy(cropRect);
    cropped.setDevicePixelRatio(targetDpr);
    const int cacheCost = qMax(1, (cropped.width() * cropped.height() * 4) / 1024);
    cache.insert(cacheKey, new QPixmap(cropped), cacheCost);
    painter->drawPixmap(targetRect.topLeft(), cropped);
}

void drawContainPixmap(QPainter *painter, const QRect &targetRect, const QPixmap &pixmap) {
    if (!painter || pixmap.isNull() || targetRect.isEmpty()) {
        return;
    }

    const qreal targetDpr = painter->device()
        ? qMax<qreal>(1.0, painter->device()->devicePixelRatioF())
        : qMax<qreal>(1.0, pixmap.devicePixelRatio());
    const qreal sourceDpr = qMax<qreal>(1.0, pixmap.devicePixelRatio());
    const QSize sourceLogicalSize(qRound(pixmap.width() / sourceDpr),
                                  qRound(pixmap.height() / sourceDpr));
    const QSize scaledSize = sourceLogicalSize.scaled(targetRect.size(), Qt::KeepAspectRatio);
    if (!scaledSize.isValid()) {
        return;
    }

    QPixmap scaled = pixmap.scaled(scaledSize * targetDpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(targetDpr);
    const QPoint topLeft(targetRect.center().x() - scaledSize.width() / 2,
                         targetRect.center().y() - scaledSize.height() / 2);
    painter->drawPixmap(topLeft, scaled);
}

QPixmap buildLinkFallbackPreview(const QUrl &url, const QString &title, const QSize &targetSize, qreal devicePixelRatio,
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
    const qreal badgeScale = hasFavicon ? 0.46 : 0.38;
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
        monoFont.setBold(true);
        monoFont.setPointSizeF(qMax(18.0, badgeSize * 0.26));
        monoFont.setLetterSpacing(QFont::PercentageSpacing, 105);
        painter.setFont(monoFont);
        painter.setPen(QColor(58, 72, 86));
        painter.drawText(badgeRect, Qt::AlignCenter, monogram);
    }

    const QRectF headlineRect(heroRect.left(), badgeRect.bottom() + 16.0, heroRect.width(), 34.0);
    QFont headlineFont = painter.font();
    headlineFont.setBold(true);
    headlineFont.setPointSizeF(qMax(11.0, bounds.height() * 0.074));
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
    return QIcon(QStringLiteral(":/resources/resources/star_filled.svg")).pixmap(size);
}

QPixmap pinPixmap(const QSize &size, bool dark) {
    const QString path = dark
        ? QStringLiteral(":/resources/resources/pin_light.svg")
        : QStringLiteral(":/resources/resources/pin.svg");
    return QIcon(path).pixmap(size);
}

QPixmap filePixmap(const QSize &size) {
    return QIcon(QStringLiteral(":/resources/resources/files.svg")).pixmap(size);
}

bool isLikelyLinkPreviewImage(const QPixmap &pixmap) {
    if (pixmap.isNull()) {
        return false;
    }

    const qreal dpr = qMax<qreal>(1.0, pixmap.devicePixelRatio());
    const QSize logicalSize(qRound(pixmap.width() / dpr), qRound(pixmap.height() / dpr));
    if (!logicalSize.isValid()) {
        return false;
    }

    const int maxDim = qMax(logicalSize.width(), logicalSize.height());
    if (maxDim < 96) {
        return false;
    }

    const qreal aspect = logicalSize.width() / qMax(1.0, static_cast<qreal>(logicalSize.height()));
    const bool squareish = aspect >= 0.85 && aspect <= 1.18;
    if (!squareish) {
        return true;
    }

    static const QSet<int> kCommonIconSizes = {
        16, 24, 32, 48, 64, 96, 128, 192, 256, 512
    };
    if (kCommonIconSizes.contains(maxDim)) {
        return false;
    }
    return maxDim >= 320;
}

QPixmap loadLocalFileIcon(const QString &filePath, const QSize &targetLogicalSize, qreal targetDpr) {
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

    static QCache<QString, QPixmap> cache(64 * 1024);
    const QString cacheKey = QStringLiteral("%1:%2:%3x%4@%5")
        .arg(filePath)
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(pixelTargetSize.width())
        .arg(pixelTargetSize.height())
        .arg(qRound(targetDpr * 100.0));
    if (QPixmap *cached = cache.object(cacheKey)) {
        return *cached;
    }

    QFileIconProvider provider;
    QIcon icon = provider.icon(info);
    QPixmap pixmap = icon.pixmap(pixelTargetSize);
    if (pixmap.isNull()) {
        return {};
    }
    pixmap.setDevicePixelRatio(qMax<qreal>(1.0, targetDpr));
    const int cacheCost = qMax(1, (pixmap.width() * pixmap.height() * 4) / 1024);
    cache.insert(cacheKey, new QPixmap(pixmap), cacheCost);
    return pixmap;
}

QString joinFileNames(const QList<QUrl> &urls) {
    QStringList names;
    names.reserve(urls.size());
    for (const QUrl &url : urls) {
        QString name;
        if (url.isLocalFile()) {
            const QFileInfo info(url.toLocalFile());
            name = info.fileName();
        } else {
            name = url.fileName();
        }
        if (name.isEmpty()) {
            name = url.toString(QUrl::FullyDecoded);
        }
        if (!name.isEmpty()) {
            names.append(name);
        }
    }
    return names.join(QStringLiteral(", "));
}

QString formatTwoLineEndElidedText(const QString &text, const QFont &font, const QFontMetrics &fontMetrics, int lineWidth) {
    if (text.isEmpty() || lineWidth <= 0) {
        return text;
    }

    QTextLayout layout(text, font);
    layout.beginLayout();

    QStringList lines;
    QString consumed;
    int textPosition = 0;
    for (int lineIndex = 0; lineIndex < 2; ++lineIndex) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(lineWidth);
        const int start = line.textStart();
        const int length = line.textLength();
        QString lineText = text.mid(start, length).trimmed();
        if (lineText.isEmpty()) {
            continue;
        }
        lines << lineText;
        textPosition = start + length;
        consumed = text.left(textPosition);
    }
    layout.endLayout();

    if (lines.isEmpty()) {
        return fontMetrics.elidedText(text, Qt::ElideRight, lineWidth);
    }

    if (textPosition >= text.size()) {
        return lines.join(QLatin1Char('\n'));
    }

    if (lines.size() == 1) {
        return fontMetrics.elidedText(text, Qt::ElideRight, lineWidth);
    }

    const QString remaining = text.mid(consumed.size()).trimmed();
    lines[1] = fontMetrics.elidedText(lines[1] + remaining, Qt::ElideRight, lineWidth);
    return lines.join(QLatin1Char('\n'));
}

bool isLikelyImageFile(const QString &filePath) {
    if (filePath.isEmpty()) {
        return false;
    }

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

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

QPixmap loadLocalImageThumbnail(const QString &filePath, const QSize &targetLogicalSize, qreal targetDpr) {
    if (!isLikelyImageFile(filePath) || !targetLogicalSize.isValid()) {
        return {};
    }

    const QFileInfo info(filePath);
    const QSize pixelTargetSize = targetLogicalSize * qMax<qreal>(1.0, targetDpr);
    if (!pixelTargetSize.isValid()) {
        return {};
    }

    static QCache<QString, QPixmap> cache(128 * 1024);
    const QString cacheKey = QStringLiteral("%1:%2:%3x%4@%5")
        .arg(filePath)
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(pixelTargetSize.width())
        .arg(pixelTargetSize.height())
        .arg(qRound(targetDpr * 100.0));
    if (QPixmap *cached = cache.object(cacheKey)) {
        return *cached;
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

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(qMax<qreal>(1.0, targetDpr));
    const int cacheCost = qMax(1, (pixmap.width() * pixmap.height() * 4) / 1024);
    cache.insert(cacheKey, new QPixmap(pixmap), cacheCost);
    return pixmap;
}

void drawLinkLabel(QPainter *painter, const QRect &rect, const QString &text, const QFont &font,
                   const QColor &color, int horizontalInset) {
    if (!painter || rect.isEmpty() || text.isEmpty()) {
        return;
    }

    painter->save();
    painter->setFont(font);
    painter->setPen(color);
    const QRect textRect = rect.adjusted(horizontalInset, 0, -horizontalInset, 0);
    QFontMetrics metrics(font);
    const bool overflow = metrics.horizontalAdvance(text) > textRect.width();
    const Qt::Alignment alignment = overflow ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignHCenter | Qt::AlignVCenter);
    painter->drawText(textRect, alignment, metrics.elidedText(text, Qt::ElideRight, textRect.width()));
    painter->restore();
}

QPixmap buildHeaderIconPixmap(const QPixmap &sourcePixmap, const QSize &targetLogicalSize, qreal targetDpr) {
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
      borderColor_(borderColor) {}

void ClipboardCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    if (!painter || !index.isValid()) {
        return;
    }

    CardData card;
    card.contentType = static_cast<ClipboardItem::ContentType>(index.data(ClipboardBoardModel::ContentTypeRole).toInt());
    card.icon = qvariant_cast<QPixmap>(index.data(ClipboardBoardModel::IconRole));
    card.thumbnail = qvariant_cast<QPixmap>(index.data(ClipboardBoardModel::ThumbnailRole));
    card.favicon = qvariant_cast<QPixmap>(index.data(ClipboardBoardModel::FaviconRole));
    card.title = index.data(ClipboardBoardModel::TitleRole).toString();
    card.url = index.data(ClipboardBoardModel::UrlRole).toString();
    card.alias = index.data(ClipboardBoardModel::AliasRole).toString();
    card.pinned = index.data(ClipboardBoardModel::PinnedRole).toBool();
    card.normalizedText = index.data(ClipboardBoardModel::NormalizedTextRole).toString();
    card.normalizedUrls = qvariant_cast<QList<QUrl>>(index.data(ClipboardBoardModel::NormalizedUrlsRole));
    card.time = index.data(ClipboardBoardModel::TimeRole).toDateTime();
    card.imageSize = index.data(ClipboardBoardModel::ImageSizeRole).toSize();
    card.color = qvariant_cast<QColor>(index.data(ClipboardBoardModel::ColorRole));
    card.favorite = index.data(ClipboardBoardModel::FavoriteRole).toBool();
    card.shortcutText = index.data(ClipboardBoardModel::ShortcutTextRole).toString();
    card.name = index.data(ClipboardBoardModel::NameRole).toString();

    if (card.name.isEmpty()) {
        return;
    }

    const int scale = MPasteSettings::getInst()->getItemScale();
    const QSize outerSize = cardOuterSizeForScale(scale);
    const QSize innerSize(kCardBaseWidth * scale / 100, kCardBaseHeight * scale / 100);
    const int topHeight = 64 * scale / 100;
    const int footerHeight = 30 * scale / 100;
    const bool hideFooter = card.contentType == ClipboardItem::Link;
    const int effectiveFooterHeight = hideFooter ? 0 : footerHeight;
    const int iconLabelSize = 48 * scale / 100;
    const int iconPixmapSize = 32 * scale / 100;
    const qreal cardRadius = qMax(10, 12 * scale / 100);
    const QRect outerRect(option.rect.topLeft(), outerSize);
    const QRect cardRect(outerRect.topLeft(), innerSize);
    const QRect topRect(cardRect.left(), cardRect.top(), cardRect.width(), topHeight);
    const QRect bodyRect(cardRect.left(), topRect.bottom(), cardRect.width(), cardRect.height() - topHeight - effectiveFooterHeight);
    const QRect footerRect(cardRect.left(), cardRect.bottom() - effectiveFooterHeight + 1, cardRect.width(), effectiveFooterHeight);

    const bool darkTheme = ThemeManager::instance()->isDark();
    const QColor baseSurface = darkTheme ? QColor(QStringLiteral("#1C232C")) : QColor(QStringLiteral("#FFFFFF"));
    const QColor bodyTextColor = darkTheme ? QColor(QStringLiteral("#D8E1EB")) : QColor(QStringLiteral("#30343B"));
    const QColor linkTitleColor = darkTheme ? QColor(QStringLiteral("#E6EDF5")) : QColor(QStringLiteral("#555555"));
    const QColor linkUrlColor = darkTheme ? QColor(QStringLiteral("#B7C3D4")) : QColor(QStringLiteral("#555555"));
    const QColor footerTextColor = darkTheme ? QColor(QStringLiteral("#93A2B3")) : QColor(QStringLiteral("#556270"));
    const QColor subtleBorderColor = darkTheme ? QColor(255, 255, 255, 24) : QColor(0, 0, 0, 18);
    const QColor topColor = dominantHeaderColor(card.icon);
    const QColor bgColor = blendColor(topColor, baseSurface, darkTheme ? 0.86 : 0.975);
    const bool selected = option.state.testFlag(QStyle::State_Selected);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    drawShadow(painter, QRectF(cardRect), cardRadius);

    QPainterPath cardPath;
    cardPath.addRoundedRect(QRectF(cardRect), cardRadius, cardRadius);

    if (card.favorite && !selected) {
        painter->setPen(QPen(QColor(247, 201, 93, 68), 5.0));
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
    const QPixmap headerIcon = buildHeaderIconPixmap(card.icon, iconPixmapRect.size(), paintDpr);
    painter->drawPixmap(iconPixmapRect.topLeft(), headerIcon);

    QFont typeFont = painter->font();
    typeFont.setPointSize(qMax(10, 12 * scale / 100));
    typeFont.setFamilies({QStringLiteral("Microsoft YaHei UI"),
                           QStringLiteral("Microsoft YaHei"),
                           QStringLiteral("Segoe UI"),
                           QStringLiteral("Segoe UI Emoji"),
                           QStringLiteral("Segoe UI Symbol"),
                           QStringLiteral("Noto Color Emoji"),
                           QStringLiteral("Noto Emoji")});
    typeFont.setBold(true);
    typeFont.setWeight(QFont::ExtraBold);
    QFont timeFont = painter->font();
    timeFont.setPointSize(qMax(8, 9 * scale / 100));
    timeFont.setFamilies({QStringLiteral("Microsoft YaHei UI"),
                           QStringLiteral("Microsoft YaHei"),
                           QStringLiteral("Segoe UI"),
                           QStringLiteral("Segoe UI Emoji"),
                           QStringLiteral("Segoe UI Symbol"),
                           QStringLiteral("Noto Color Emoji"),
                           QStringLiteral("Noto Emoji")});

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
    const QString timeLabel = QLocale::system().toString(card.time, QLocale::ShortFormat);
    const QString aliasLabel = card.alias.trimmed();
    if (!aliasLabel.isEmpty()) {
        drawElidedText(painter, typeRect, aliasLabel, typeFont, QColor(Qt::white), Qt::AlignLeft | Qt::AlignVCenter);

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
        drawElidedText(painter, typeMetaRect, typeLabel, metaTypeFont, QColor(Qt::white), Qt::AlignLeft | Qt::AlignVCenter);

        const QString timeSuffix = QStringLiteral("- %1").arg(timeLabel);
        const int timeX = timeRect.left() + typeDrawWidth + metaGap;
        if (timeX < timeRect.right()) {
            const QRect timeMetaRect(timeX, timeRect.top(),
                                     timeRect.right() - timeX, timeRect.height());
            drawElidedText(painter, timeMetaRect, timeSuffix, timeFont, QColor(Qt::white), Qt::AlignLeft | Qt::AlignVCenter);
        }
    } else {
        drawElidedText(painter, typeRect, typeLabel, typeFont, QColor(Qt::white), Qt::AlignLeft | Qt::AlignVCenter);
        drawElidedText(painter, timeRect, timeLabel,
                       timeFont, QColor(Qt::white), Qt::AlignLeft | Qt::AlignVCenter);
    }

    const QRect previewRect = bodyRect.adjusted(10 * scale / 100, 8 * scale / 100, -10 * scale / 100, -6 * scale / 100);
    const QRect imagePreviewRect = bodyRect;
    switch (card.contentType) {
        case ClipboardItem::Image:
            if (!card.thumbnail.isNull()) {
                drawCoverPixmap(painter, imagePreviewRect, card.thumbnail, card.name, card.imageSize);
            } else {
                QFont previewFont = painter->font();
                previewFont.setPointSize(qMax(9, 10 * scale / 100));
                drawWrappedText(painter, imagePreviewRect.adjusted(10 * scale / 100, 8 * scale / 100, -10 * scale / 100, -6 * scale / 100),
                                previewTextForCard(card), previewFont, bodyTextColor);
            }
            break;
        case ClipboardItem::Office:
            if (!card.thumbnail.isNull()) {
                drawContainPixmap(painter, imagePreviewRect, card.thumbnail);
            } else {
                QFont previewFont = painter->font();
                previewFont.setPointSize(qMax(9, 10 * scale / 100));
                drawWrappedText(painter, imagePreviewRect.adjusted(10 * scale / 100, 8 * scale / 100, -10 * scale / 100, -6 * scale / 100),
                                previewTextForCard(card), previewFont, bodyTextColor);
            }
            break;
        case ClipboardItem::RichText:
            if (!card.thumbnail.isNull()) {
                drawCoverPixmap(painter, previewRect, card.thumbnail, card.name, card.imageSize);
            } else {
                QFont previewFont = painter->font();
                previewFont.setPointSize(qMax(9, 10 * scale / 100));
                drawWrappedText(painter, previewRect, previewTextForCard(card), previewFont, bodyTextColor);
            }
            break;
        case ClipboardItem::Color: {
            const QColor color = card.color.isValid() ? card.color : QColor(QStringLiteral("#4A5F7A"));
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawRoundedRect(previewRect, 10.0, 10.0);
            QFont previewFont = painter->font();
            previewFont.setPointSize(qMax(10, 12 * scale / 100));
            previewFont.setBold(true);
            const QColor fontColor(255 - color.red(), 255 - color.green(), 255 - color.blue());
            drawElidedText(painter, previewRect, previewTextForCard(card), previewFont, fontColor, Qt::AlignCenter);
            break;
        }
        case ClipboardItem::Link: {
            const int titleHeight = qMax(20, 20 * scale / 100);
            const int urlHeight = qMax(20, 24 * scale / 100);
            const int rowSpacing = qMax(1, scale / 100);
            const QRect imageRect(bodyRect.left(),
                                  bodyRect.top(),
                                  bodyRect.width(),
                                  qMax(1, bodyRect.height() - titleHeight - urlHeight - rowSpacing * 2));
            const QRect titleRect(bodyRect.left(),
                                  imageRect.bottom() + 1 + rowSpacing,
                                  bodyRect.width(),
                                  titleHeight);
            const QRect urlRect(bodyRect.left(),
                                titleRect.bottom() + 1 + rowSpacing,
                                bodyRect.width(),
                                urlHeight);
            const QString linkText = card.url.isEmpty() ? card.normalizedText.trimmed() : card.url;
            const QUrl currentUrl(linkText);
            QPixmap linkPreviewImage;
            if (!card.thumbnail.isNull()) {
                linkPreviewImage = card.thumbnail;
            } else if (!card.favicon.isNull() && isLikelyLinkPreviewImage(card.favicon)) {
                linkPreviewImage = card.favicon;
            }

            if (!linkPreviewImage.isNull()) {
                drawCoverPixmap(painter, imageRect, linkPreviewImage, card.name, card.imageSize);
            } else {
                const qreal paintDpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
                const QPixmap fallbackPreview = buildLinkFallbackPreview(currentUrl, card.title, imageRect.size(), paintDpr, card.favicon);
                if (!fallbackPreview.isNull()) {
                    painter->drawPixmap(imageRect.topLeft(), fallbackPreview);
                }
            }

            QFont linkTitleFont = painter->font();
            linkTitleFont.setPointSize(qMax(9, 9 * scale / 100));
            linkTitleFont.setBold(true);
            QFont linkUrlFont = painter->font();
            linkUrlFont.setPointSize(qMax(8, 9 * scale / 100));
            linkUrlFont.setUnderline(true);
            const int linkPadding = qMax(6, 8 * scale / 100);
            QRect adjustedUrlRect = urlRect;
            QRect shortcutRect;
            QFont shortcutFont;
            const QString shortcutText = card.shortcutText;
            bool drawShortcut = false;
            if (!shortcutText.isEmpty()) {
                shortcutFont = painter->font();
                shortcutFont.setPointSize(qMax(8, 8 * scale / 100));
                const QFontMetrics shortcutMetrics(shortcutFont);
                const int shortcutPadding = qMax(6, 8 * scale / 100);
                const int shortcutTextWidth = shortcutMetrics.horizontalAdvance(shortcutText) + shortcutPadding;
                const int shortcutMinWidth = qMax(48, 56 * scale / 100);
                int shortcutWidth = qMax(shortcutMinWidth, shortcutTextWidth);
                const int maxShortcutWidth = qMax(0, urlRect.width() - linkPadding * 2);
                shortcutWidth = qMin(shortcutWidth, maxShortcutWidth);
                if (shortcutWidth > 0) {
                    adjustedUrlRect = urlRect.adjusted(0, 0, -(shortcutWidth + linkPadding), 0);
                    shortcutRect = QRect(
                        urlRect.right() - linkPadding - shortcutWidth + 1,
                        urlRect.top(),
                        shortcutWidth,
                        urlRect.height());
                    drawShortcut = true;
                }
            }
            drawLinkLabel(painter,
                          titleRect,
                          card.title.isEmpty() ? fallbackHeadline(QString(), currentUrl) : card.title,
                          linkTitleFont,
                          linkTitleColor,
                          linkPadding);
            drawLinkLabel(painter,
                          adjustedUrlRect,
                          linkText,
                          linkUrlFont,
                          linkUrlColor,
                          linkPadding);
            if (drawShortcut) {
                drawElidedText(painter,
                               shortcutRect,
                               shortcutText,
                               shortcutFont,
                               footerTextColor,
                               Qt::AlignRight | Qt::AlignVCenter);
            }
            break;
        }
        case ClipboardItem::File: {
            if (card.normalizedUrls.size() == 1 && card.normalizedUrls.first().isLocalFile()) {
                const QString filePath = card.normalizedUrls.first().toLocalFile();
                const QPixmap thumb = loadLocalImageThumbnail(filePath, imagePreviewRect.size(), paintDpr);
                if (!thumb.isNull()) {
                    drawCoverPixmap(painter, imagePreviewRect, thumb, card.name, QSize());
                    break;
                }
            }

            if (card.normalizedUrls.size() == 1) {
                const QString filePath = card.normalizedUrls.first().isLocalFile()
                    ? card.normalizedUrls.first().toLocalFile()
                    : QString();
                const int iconPadding = qMax(10, 12 * scale / 100);
                const int iconSize = qMax(24, qMin(previewRect.width() - iconPadding * 2,
                                                   previewRect.height() - iconPadding * 2));
                const QRect iconRect(previewRect.left() + (previewRect.width() - iconSize) / 2,
                                     previewRect.top() + (previewRect.height() - iconSize) / 2,
                                     iconSize,
                                     iconSize);
                QPixmap iconPixmap = filePath.isEmpty()
                    ? QPixmap()
                    : loadLocalFileIcon(filePath, iconRect.size(), paintDpr);
                if (iconPixmap.isNull()) {
                    iconPixmap = filePixmap(iconRect.size());
                }
                QPoint iconPos = iconRect.topLeft();
                const qreal iconDpr = qMax<qreal>(1.0, iconPixmap.devicePixelRatio());
                const QSize iconLogicalSize(qRound(iconPixmap.width() / iconDpr),
                                            qRound(iconPixmap.height() / iconDpr));
                if (iconLogicalSize.isValid() && iconLogicalSize != iconRect.size()) {
                    iconPos.setX(iconRect.left() + (iconRect.width() - iconLogicalSize.width()) / 2);
                    iconPos.setY(iconRect.top() + (iconRect.height() - iconLogicalSize.height()) / 2);
                }
                painter->drawPixmap(iconPos, iconPixmap);
                break;
            }

            if (card.normalizedUrls.size() > 1) {
                QFont previewFont = painter->font();
                previewFont.setPointSize(qMax(9, 10 * scale / 100));
                const QFontMetrics metrics(previewFont);
                const int textHeight = qMax(metrics.height() * 2 + 4, 28 * scale / 100);
                const int textSidePadding = qMax(8, 10 * scale / 100);
                const QRect textRect(previewRect.left() + textSidePadding,
                                     previewRect.bottom() - textHeight,
                                     previewRect.width() - textSidePadding * 2,
                                     textHeight);
                const int iconSidePadding = qMax(10, 12 * scale / 100);
                const int iconMaxSize = qMin(previewRect.width() - iconSidePadding * 2,
                                             qMax(24, previewRect.height() - textHeight - 8 * scale / 100));
                const QRect iconRect(previewRect.left() + (previewRect.width() - iconMaxSize) / 2,
                                     previewRect.top() + qMax(2, 4 * scale / 100),
                                     iconMaxSize,
                                     iconMaxSize);
                painter->drawPixmap(iconRect, filePixmap(iconRect.size()));

                const QString fileNames = joinFileNames(card.normalizedUrls);
                const QString formatted = formatTwoLineEndElidedText(fileNames, previewFont, metrics, textRect.width());
                painter->save();
                painter->setFont(previewFont);
                painter->setPen(bodyTextColor);
                painter->drawText(textRect, Qt::AlignLeft | Qt::AlignTop, formatted);
                painter->restore();
                break;
            }

            const int glyphSize = qMin(previewRect.width() / 4, previewRect.height() / 3);
            const QRect glyphRect(previewRect.left() + 4 * scale / 100, previewRect.top(), glyphSize, glyphSize);
            painter->drawPixmap(glyphRect, filePixmap(glyphRect.size()));
            QFont previewFont = painter->font();
            previewFont.setPointSize(qMax(9, 10 * scale / 100));
            drawWrappedText(painter, previewRect.adjusted(glyphSize + 12 * scale / 100, 2 * scale / 100, -4 * scale / 100, -2 * scale / 100),
                            previewTextForCard(card), previewFont, bodyTextColor);
            break;
        }
        case ClipboardItem::Text:
        case ClipboardItem::All: {
            QFont previewFont = painter->font();
            previewFont.setPointSize(qMax(9, 10 * scale / 100));
            drawWrappedText(painter, previewRect, previewTextForCard(card), previewFont, bodyTextColor);
            break;
        }
    }

    if (!hideFooter) {
        QFont footerFont = painter->font();
        footerFont.setPointSize(qMax(8, 8 * scale / 100));
        const int footerPadding = qMax(6, 8 * scale / 100);
        const QFontMetrics footerMetrics(footerFont);
        const QString shortcutText = card.shortcutText;
        const int shortcutPadding = qMax(6, 8 * scale / 100);
        const int shortcutTextWidth = shortcutText.isEmpty()
            ? 0
            : footerMetrics.horizontalAdvance(shortcutText) + shortcutPadding;
        const int shortcutMinWidth = qMax(48, 56 * scale / 100);
        const int shortcutWidth = shortcutText.isEmpty()
            ? 0
            : qMax(shortcutMinWidth, shortcutTextWidth);
        const QRect shortcutRect(
            footerRect.right() - footerPadding - shortcutWidth + 1,
            footerRect.top(),
            shortcutWidth,
            footerRect.height());
        const QRect countRect(
            footerRect.left() + footerPadding,
            footerRect.top(),
            footerRect.width() - footerPadding * 2 - shortcutWidth,
            footerRect.height());
        const bool isSingleFilePath = card.contentType == ClipboardItem::File && card.normalizedUrls.size() == 1;
        drawElidedText(painter, countRect, countLabelForCard(card), footerFont, footerTextColor,
                       Qt::AlignLeft | Qt::AlignVCenter,
                       isSingleFilePath ? Qt::ElideMiddle : Qt::ElideRight);
        if (!shortcutText.isEmpty()) {
            drawElidedText(painter, shortcutRect, shortcutText, footerFont, footerTextColor,
                           Qt::AlignRight | Qt::AlignVCenter);
        }
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
