#include "CardBodyRenderer.h"

#include <QRegularExpression>
#include <cmath>
#include <QImage>

#include <QApplication>
#include <QCache>
#include <QFileInfo>
#include <QHash>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainterPath>
#include <QTextLayout>
#include <QObject>
#include <QSet>
#include <QUrl>

#include "widget/CardTheme.h"

// ---- font family helpers (same logic as ClipboardCardDelegate.cpp) --------

namespace {

QString chooseInstalledFontFamily(const QStringList &preferredFamilies, const QString &fallbackFamily) {
    const QFontDatabase database;
    const QStringList availableFamilies = database.families();
    for (const QString &family : preferredFamilies) {
        if (availableFamilies.contains(family, Qt::CaseInsensitive)) {
            return family;
        }
    }
    return fallbackFamily;
}

const QString &preferredUiFontFamily() {
#ifdef Q_OS_WIN
    static const QString family = chooseInstalledFontFamily(
        {
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Microsoft YaHei"),
            QStringLiteral("Segoe UI"),
            QStringLiteral("Noto Sans"),
            QStringLiteral("Arial")
        },
        QApplication::font().family());
#else
    // Follow the desktop UI font; YaHei on Linux has no medium weights.
    static const QString family = QApplication::font().family();
#endif
    return family;
}

const QString &preferredMonoFontFamily() {
    static const QString family = chooseInstalledFontFamily(
        {
            QStringLiteral("Cascadia Mono"),
            QStringLiteral("Consolas"),
            QStringLiteral("Cascadia Code"),
            QStringLiteral("Courier New"),
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Segoe UI")
        },
        QApplication::font().family());
    return family;
}

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

QCache<QString, QPixmap> &coverPixmapCache() {
    static QCache<QString, QPixmap> cache(60);
    return cache;
}

void drawLoadingPlaceholder(QPainter *painter,
                            const QRect &rect,
                            int scale,
                            bool darkTheme,
                            int phase) {
    if (!painter || !rect.isValid()) {
        return;
    }

    const CardTheme theme = CardTheme::forCurrentTheme();
    const QColor base = theme.placeholderBase;
    const QColor highlight = theme.placeholderHighlight;
    const qreal t = (phase % 100) / 100.0;

    QLinearGradient gradient(rect.topLeft(), rect.topRight());
    gradient.setColorAt(0.0, base);
    gradient.setColorAt(qMax(0.0, t - 0.15), base);
    gradient.setColorAt(t, highlight);
    gradient.setColorAt(qMin(1.0, t + 0.15), base);
    gradient.setColorAt(1.0, base);

    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(gradient);
    const qreal radius = qMax(10.0, 12 * scale / 100.0);
    painter->drawRoundedRect(rect.adjusted(4, 4, -4, -4), radius, radius);

    QFont font = painter->font();
    CardRenderUtils::applyUiFontDefaults(font);
    font.setPointSize(qMax(8, 9 * scale / 100));
    painter->setFont(font);
    painter->setPen(theme.placeholderTextColor);
    const int dots = phase % 4;
    const QString text = dots == 0
        ? QObject::tr("Loading")
        : QObject::tr("Loading") + QString(dots, QLatin1Char('.'));
    painter->drawText(rect, Qt::AlignCenter, text);
    painter->restore();
}

void drawUnavailablePlaceholder(QPainter *painter,
                                const QRect &rect,
                                int scale,
                                bool darkTheme) {
    if (!painter || !rect.isValid()) {
        return;
    }

    painter->save();
    painter->setPen(Qt::NoPen);
    const CardTheme theme = CardTheme::forCurrentTheme();
    painter->setBrush(theme.unavailableBgColor);
    const qreal radius = qMax(10.0, 12 * scale / 100.0);
    painter->drawRoundedRect(rect.adjusted(4, 4, -4, -4), radius, radius);

    QFont font = painter->font();
    CardRenderUtils::applyUiFontDefaults(font);
    font.setPointSize(qMax(8, 9 * scale / 100));
    painter->setFont(font);
    painter->setPen(theme.unavailableTextColor);
    painter->drawText(rect, Qt::AlignCenter, QObject::tr("Preview unavailable"));
    painter->restore();
}

} // anonymous namespace

// ---- CardRenderUtils implementations ----

void CardRenderUtils::clearCoverPixmapCache() {
    coverPixmapCache().clear();
}

void CardRenderUtils::forgetCoverPixmaps(const QString &name) {
    if (name.isEmpty()) {
        return;
    }
    const QString prefix = name + QLatin1Char(':');
    const QList<QString> keys = coverPixmapCache().keys();
    for (const QString &key : keys) {
        if (key.startsWith(prefix)) {
            coverPixmapCache().remove(key);
        }
    }
}

void CardRenderUtils::applyUiFontDefaults(QFont &font) {
    font.setFamily(preferredUiFontFamily());
    font.setStyleHint(QFont::SansSerif, QFont::PreferDefault);
}

void CardRenderUtils::applyMonoFontDefaults(QFont &font) {
    font.setFamily(preferredMonoFontFamily());
    font.setStyleHint(QFont::Monospace, QFont::PreferDefault);
}

void CardRenderUtils::drawElidedText(QPainter *painter, const QRect &rect, const QString &text,
                                      const QFont &font, const QColor &color, Qt::Alignment alignment,
                                      Qt::TextElideMode elideMode) {
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

void CardRenderUtils::drawWrappedText(QPainter *painter, const QRect &rect, const QString &text,
                                       const QFont &font, const QColor &color,
                                       qreal lineSpacing) {
    if (text.isEmpty() || rect.isEmpty()) {
        return;
    }

    painter->save();
    painter->setFont(font);
    painter->setPen(color);

    // Always use QTextLayout for consistent line spacing control.
    // QPainter::drawText with TextWordWrap can produce overlapping
    // lines on certain fonts / DPI configurations.
    const qreal effectiveSpacing = lineSpacing > 0 ? lineSpacing : 1.2;
    // QTextLayout breaks lines only at U+2028, not at '\n': keep the text's own lines.
    QString laidOut = text;
    laidOut.replace(QLatin1String("\r\n"), QString(QChar::LineSeparator));
    laidOut.replace(QLatin1Char('\n'), QChar::LineSeparator);
    QTextLayout layout(laidOut, font, painter->device());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);

    const QFontMetricsF fm(font);
    const qreal minLineH = fm.height() * effectiveSpacing;

    // First pass: create and position lines that fit.
    // Track how many lines were successfully positioned.
    layout.beginLayout();
    qreal y = 0;
    int lineCount = 0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(rect.width());
        const qreal lineH = qMax(line.height() * effectiveSpacing, minLineH);
        if (y + lineH > rect.height()) {
            // This line doesn't fit.  Move it far offscreen so
            // layout.draw() won't paint it over the first line.
            line.setPosition(QPointF(0, rect.height() + 9999));
            break;
        }
        line.setPosition(QPointF(0, y));
        y += lineH;
        ++lineCount;
    }
    layout.endLayout();
    layout.draw(painter, rect.topLeft());

    painter->restore();
}

void CardRenderUtils::drawCoverPixmap(QPainter *painter, const QRect &targetRect, const QPixmap &pixmap,
                                       const QString &debugName, const QSize &debugImageSize) {
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
    if (sourceLogicalSize == targetRect.size()) {
        logTallImageCoverEvent(debugName, debugImageSize, targetRect.size(), targetDpr, pixmap, "direct");
        painter->drawPixmap(targetRect.topLeft(), pixmap);
        return;
    }

    // Use the item name (stable) as cache key instead of pixmap.cacheKey()
    // which changes every time the item is reloaded from disk.
    const QString cacheKey = QStringLiteral("%1:%2x%3@%4")
        .arg(debugName.isEmpty() ? QString::number(static_cast<qulonglong>(pixmap.cacheKey())) : debugName)
        .arg(pixelTargetSize.width())
        .arg(pixelTargetSize.height())
        .arg(qRound(targetDpr * 100.0));
    if (QPixmap *cached = coverPixmapCache().object(cacheKey)) {
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
    coverPixmapCache().insert(cacheKey, new QPixmap(cropped), 1);
    painter->drawPixmap(targetRect.topLeft(), cropped);
}

QRect CardRenderUtils::insetPreviewRect(const QRect &bodyRect, int scale) {
    const int side = qMax(6, 8 * scale / 100);
    const int top = qMax(4, 6 * scale / 100);
    return bodyRect.adjusted(side, top, -side, -qMax(2, 2 * scale / 100));
}

void CardRenderUtils::drawInsetVisualPreview(QPainter *painter, const QRect &bodyRect, const CardData &card,
                                             int scale, bool darkTheme, int loadingPhase, bool containMode) {
    // Framed like the rich-text panel instead of bleeding into the header and edges.
    const QRect panelRect = insetPreviewRect(bodyRect, scale);
    const qreal radius = qMax(6, 8 * scale / 100);
    QPainterPath panel;
    panel.addRoundedRect(QRectF(panelRect), radius, radius);
    painter->save();
    painter->setClipPath(panel, Qt::IntersectClip);
    drawManagedVisualPreview(painter, panelRect, card, scale, darkTheme, loadingPhase, containMode);
    painter->restore();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(darkTheme ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 18), 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(panel);
    painter->restore();
}

void CardRenderUtils::drawContainPixmap(QPainter *painter, const QRect &targetRect, const QPixmap &pixmap) {
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

void CardRenderUtils::drawManagedVisualPreview(QPainter *painter, const QRect &rect, const CardData &card,
                                                int scale, bool darkTheme, int loadingPhase, bool containMode) {
    if (!painter || !rect.isValid()) {
        return;
    }

    switch (card.previewState) {
        case ClipboardBoardModel::PreviewReady:
            if (!card.thumbnail.isNull()) {
                if (containMode) {
                    drawContainPixmap(painter, rect, card.thumbnail);
                } else {
                    drawCoverPixmap(painter, rect, card.thumbnail, card.name, card.imageSize);
                }
            }
            // Thumbnail may have been released after card caching.
            // Don't fall through to Loading -- the cached card pixmap
            // already has the rendered preview.
            return;
        case ClipboardBoardModel::PreviewLoading:
            drawLoadingPlaceholder(painter, rect, scale, darkTheme, loadingPhase);
            return;
        case ClipboardBoardModel::PreviewUnavailable:
            drawUnavailablePlaceholder(painter, rect, scale, darkTheme);
            return;
        case ClipboardBoardModel::PreviewNotApplicable:
            break;
    }

    if (!card.thumbnail.isNull()) {
        if (containMode) {
            drawContainPixmap(painter, rect, card.thumbnail);
        } else {
            drawCoverPixmap(painter, rect, card.thumbnail, card.name, card.imageSize);
        }
    } else {
        drawUnavailablePlaceholder(painter, rect, scale, darkTheme);
    }
}

QString CardRenderUtils::previewTextForCard(const CardData &card) {
    switch (card.contentType) {
        case File: {
            QStringList lines;
            for (const QUrl &url : card.normalizedUrls) {
                const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
                const QFileInfo info(path);
                lines << (info.fileName().isEmpty() ? path : info.fileName());
            }
            return lines.join(QLatin1Char('\n'));
        }
        case Link: {
            QString linkUrl;
            if (!card.url.isEmpty()) {
                linkUrl = card.url;
            } else if (!card.normalizedUrls.isEmpty()) {
                const QUrl &first = card.normalizedUrls.first();
                linkUrl = first.isLocalFile() ? first.toLocalFile() : first.toDisplayString(QUrl::PreferLocalFile);
            } else {
                linkUrl = card.normalizedText.left(512).trimmed();
            }
            if (!card.title.trimmed().isEmpty()) {
                return card.title.trimmed() + QLatin1Char('\n') + linkUrl;
            }
            return linkUrl;
        }
        case Color:
            return card.color.name(QColor::HexRgb);
        case RichText:
            return card.normalizedText.trimmed().isEmpty()
                ? QObject::tr("Rich text preview")
                : card.normalizedText.trimmed();
        case Image:
            return QObject::tr("Image preview");
        case Office:
            if (!card.normalizedText.trimmed().isEmpty()) {
                return card.normalizedText.trimmed();
            }
            return QObject::tr("Office shape preview");
        case Text:
        case All:
            break;
    }
    return card.normalizedText.trimmed();
}

bool CardRenderUtils::isPlaceholderIcon(const QPixmap &icon) {
    if (icon.isNull()) {
        return true;
    }
    static QCache<qint64, bool> cache(512);
    if (const bool *hit = cache.object(icon.cacheKey())) {
        return *hit;
    }
    auto signature = [](const QPixmap &pixmap) {
        return pixmap.toImage().convertToFormat(QImage::Format_ARGB32)
            .scaled(12, 12, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    };
    static const QImage reference = signature(QPixmap(QStringLiteral(":/resources/resources/unknown.svg")));
    const QImage sample = signature(icon);
    qint64 difference = 0;
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12; ++x) {
            const QRgb a = sample.pixel(x, y), b = reference.pixel(x, y);
            difference += qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b))
                + qAbs(qBlue(a) - qBlue(b)) + qAbs(qAlpha(a) - qAlpha(b));
        }
    }
    const bool placeholder = difference / (12 * 12 * 4) < 14;
    cache.insert(icon.cacheKey(), new bool(placeholder));
    return placeholder;
}

void CardRenderUtils::paintCodeBlock(QPainter *painter, const QRect &bodyRect, const QString &text, int scale,
                                     bool darkTheme, const QColor &textColor) {
    QFont font = painter->font();
    applyMonoFontDefaults(font);
    font.setPointSize(qMax(8, 9 * scale / 100));

    QString code = text;
    if (QFontMetrics(font).inFont(QChar(0x2011))) {
        code.replace(QLatin1Char('-'), QChar(0x2011));
    }

    // The block shares the left edge of the header icon and the footer.
    const int edge = qMax(8, 12 * scale / 100);
    const int pad = qMax(6, 8 * scale / 100);
    const QRect available = bodyRect.adjusted(edge, qMax(6, 10 * scale / 100), -edge, -qMax(4, 6 * scale / 100));
    const QRect textArea = available.adjusted(pad, pad, -pad, -pad);
    if (textArea.width() <= 0 || textArea.height() <= 0) {
        return;
    }

    // Height of the lines that fit, so a one-line command gets a one-line block.
    constexpr qreal kLineSpacing = 1.25;
    QString laidOut = code;
    laidOut.replace(QLatin1String("\r\n"), QString(QChar::LineSeparator));
    laidOut.replace(QLatin1Char('\n'), QChar::LineSeparator);
    QTextLayout layout(laidOut, font, painter->device());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    const qreal minLine = QFontMetricsF(font).height() * kLineSpacing;
    qreal used = 0;
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(textArea.width());
        const qreal lineHeight = qMax(line.height() * kLineSpacing, minLine);
        if (used + lineHeight > textArea.height()) {
            break;
        }
        used += lineHeight;
    }
    layout.endLayout();

    const int blockHeight = qMin(available.height(), int(std::ceil(used)) + pad * 2);
    const QRectF block(available.left(), available.top(), available.width(), blockHeight);
    const qreal radius = qMax(5, 8 * scale / 100);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(darkTheme ? QColor(255, 255, 255, 16) : QColor(0, 0, 0, 16), 1));
    painter->setBrush(darkTheme ? QColor(0, 0, 0, 64) : QColor(255, 255, 255, 150));
    painter->drawRoundedRect(block.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter->restore();

    drawWrappedText(painter, QRect(textArea.left(), textArea.top(), textArea.width(), blockHeight - pad * 2),
                    code, font, textColor, kLineSpacing);
}

QUrl CardRenderUtils::singleWebUrl(const QString &text) {
    const QString value = text.trimmed();
    if (value.isEmpty() || value.size() > 2048 || value.contains(QRegularExpression(QStringLiteral("\\s")))) {
        return {};
    }
    if (!value.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !value.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        return {};
    }
    const QUrl url(value, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty() ? url : QUrl();
}

QString CardRenderUtils::displayHost(const QUrl &url) {
    QString host = url.host();
    if (host.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) {
        host = host.mid(4);
    }
    return host;
}

namespace {
// "/a/b/" as "a", "b".
QStringList webPathSegments(const QUrl &url) {
    return url.path(QUrl::FullyDecoded).split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

// A page title that only names the site ("Reddit" for reddit.com/r/gnome/,
// what some sites give a reader that is not a browser) says less than the path.
bool titleNamesOnlySite(const QString &title, const QString &host) {
    QString name;
    for (const QChar ch : title) {
        if (ch.isLetterOrNumber()) {
            name += ch.toLower();
        }
    }
    if (name.isEmpty()) {
        return true;
    }
    const QStringList labels = host.toLower().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    QString hostName = labels.join(QString());
    hostName.remove(QLatin1Char('-'));
    const QString siteLabel = labels.size() >= 2 ? labels.at(labels.size() - 2) : hostName;
    return name == hostName || name == QString(siteLabel).remove(QLatin1Char('-'));
}

// A colour of its own for each site, from its name.
QColor webAccent(const QString &host) {
    const uint h = qHash(host);
    return QColor::fromHslF((h % 360) / 360.0, 0.55, 0.48);
}

// The site's colour: its icon's when that has one (the mean of its coloured
// pixels), else one of its own from its name.
QColor siteColor(const QString &host, const QPixmap &favicon) {
    if (!favicon.isNull()) {
        static QHash<qint64, QColor> cache;
        if (const auto it = cache.constFind(favicon.cacheKey()); it != cache.constEnd()) {
            if (it->isValid()) {
                return *it;
            }
        } else {
            const QImage image = favicon.toImage().scaled(24, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                     .convertToFormat(QImage::Format_ARGB32);
            qreal r = 0, g = 0, b = 0, weight = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = QColor::fromRgba(image.pixel(x, y));
                    if (c.alpha() < 128 || c.hsvSaturationF() < 0.25 || c.valueF() < 0.2) {
                        continue;
                    }
                    const qreal w = c.alphaF() * c.hsvSaturationF();
                    r += c.redF() * w;
                    g += c.greenF() * w;
                    b += c.blueF() * w;
                    weight += w;
                }
            }
            QColor found;
            if (weight > 2.0) {
                // Strong enough to carry white, as the band does.
                const QColor hsl = QColor::fromRgbF(r / weight, g / weight, b / weight).toHsl();
                found = QColor::fromHslF(qMax<qreal>(0.0, hsl.hslHueF()), qMax<qreal>(0.45, hsl.hslSaturationF()),
                                         qBound<qreal>(0.38, hsl.lightnessF(), 0.55));
            }
            if (cache.size() > 256) {
                cache.clear();
            }
            cache.insert(favicon.cacheKey(), found);
            if (found.isValid()) {
                return found;
            }
        }
    }
    return webAccent(host);
}

// The site's name without its kind and country: "news.example.com" as "example".
QString siteName(const QString &host) {
    QStringList labels = host.toLower().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (labels.size() >= 3 && labels.at(labels.size() - 2).size() <= 3 && labels.last().size() == 2) {
        labels.removeLast();   // example.co.uk
    }
    return labels.size() >= 2 ? labels.at(labels.size() - 2) : host;
}

// What an address says about its page, read from its path: a headline (the
// last part that reads as words, "what_is_your_favourite_setup" as "What is
// your favourite setup") and the context before it ("r/linux"), leaving out
// numbers, ids ("1xjtutb") and words every site uses ("comments", "post").
struct WebPathSummary {
    QString headline;
    QString context;
};

bool isPathNoise(const QString &segment) {
    static const QSet<QString> noise = {
        QStringLiteral("comments"), QStringLiteral("comment"), QStringLiteral("post"), QStringLiteral("posts"),
        QStringLiteral("p"), QStringLiteral("article"), QStringLiteral("articles"), QStringLiteral("blob"),
        QStringLiteral("tree"), QStringLiteral("status"), QStringLiteral("watch"), QStringLiteral("s"),
        QStringLiteral("t"), QStringLiteral("en"), QStringLiteral("en-us"), QStringLiteral("zh-cn"),
        QStringLiteral("index"), QStringLiteral("item"), QStringLiteral("items"), QStringLiteral("view"),
        QStringLiteral("detail"), QStringLiteral("details"), QStringLiteral("d"), QStringLiteral("html"),
    };
    return noise.contains(segment.toLower());
}

bool isPathId(const QString &segment) {
    int digits = 0;
    int letters = 0;
    for (const QChar ch : segment) {
        if (ch.isDigit()) {
            ++digits;
        } else if (ch.isLetter()) {
            ++letters;
        } else {
            return false;   // A separator: words, not an id.
        }
    }
    // All digits (an id, a year, a day), or letters and digits mixed in one run.
    return letters == 0 || (digits > 0 && segment.size() >= 5);
}

QString humanizedSlug(const QString &segment) {
    QString text = segment;
    text.replace(QRegularExpression(QStringLiteral("[_+]+")), QStringLiteral(" "));
    const QStringList words = text.split(QRegularExpression(QStringLiteral("[\\s-]+")), Qt::SkipEmptyParts);
    // Three words or more are a sentence; "example-repo" stays a name.
    if (words.size() < 3) {
        return text.simplified();
    }
    QString sentence = words.join(QLatin1Char(' '));
    sentence[0] = sentence.at(0).toUpper();
    return sentence;
}

WebPathSummary summarizeWebPath(const QUrl &url) {
    QStringList segments = webPathSegments(url);
    if (!segments.isEmpty()) {
        static const QRegularExpression pageExtension(QStringLiteral("\\.(?:html?|php|aspx?|jsp)$"),
                                                      QRegularExpression::CaseInsensitiveOption);
        segments.last().remove(pageExtension);
    }
    // "r", "linux" as "r/linux": a one-letter part names the kind of the next.
    QStringList parts;
    for (int i = 0; i < segments.size(); ++i) {
        const QString &segment = segments.at(i);
        if (segment.size() == 1 && !isPathNoise(segment) && i + 1 < segments.size() && !isPathId(segments.at(i + 1))) {
            parts << segment + QLatin1Char('/') + segments.at(++i);
        } else if (!segment.isEmpty() && !isPathNoise(segment) && !isPathId(segment)) {
            parts << segment;
        }
    }
    WebPathSummary summary;
    if (parts.isEmpty()) {
        return summary;
    }
    // A section of a page ("example-repo/issues") names the page, the section
    // goes with where it is: "example-user › Issues" over "example-repo".
    static const QSet<QString> sections = {
        QStringLiteral("issues"), QStringLiteral("pulls"), QStringLiteral("pull"), QStringLiteral("releases"),
        QStringLiteral("wiki"), QStringLiteral("discussions"), QStringLiteral("actions"), QStringLiteral("commits"),
        QStringLiteral("tags"), QStringLiteral("branches"), QStringLiteral("projects"), QStringLiteral("about"),
    };
    QString section;
    if (parts.size() >= 2 && sections.contains(parts.last().toLower())) {
        section = parts.takeLast();
        section[0] = section.at(0).toUpper();
    }
    summary.headline = humanizedSlug(parts.takeLast());
    if (parts.size() > 2) {
        parts = parts.mid(parts.size() - 2);
    }
    if (!section.isEmpty()) {
        parts << section;
    }
    summary.context = parts.join(QStringLiteral(" \u203A "));
    return summary;
}

// Wrapped at words, at most `maxLines`, the last one elided when cut short.
// Returns the height used.
int drawClampedText(QPainter *painter, const QRect &rect, const QString &text, const QFont &font,
                    const QColor &color, int maxLines, qreal lineSpacing = 1.15) {
    if (text.isEmpty() || rect.isEmpty() || maxLines <= 0) {
        return 0;
    }
    const QFontMetricsF fm(font, painter->device());
    const qreal lineH = fm.height() * lineSpacing;
    const int fitLines = qMax(1, qMin(maxLines, int(rect.height() / lineH)));
    QTextLayout layout(text, font, painter->device());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    QStringList lines;
    int consumed = 0;
    while (lines.size() < fitLines) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(rect.width());
        lines << text.mid(line.textStart(), line.textLength());
        consumed = line.textStart() + line.textLength();
    }
    layout.endLayout();
    if (!lines.isEmpty() && consumed < text.size()) {
        lines.last() = fm.elidedText(lines.last().trimmed() + QStringLiteral(" ") + text.mid(consumed),
                                     Qt::ElideRight, rect.width());
    }
    painter->save();
    painter->setFont(font);
    painter->setPen(color);
    qreal y = rect.top();
    for (const QString &line : lines) {
        painter->drawText(QRectF(rect.left(), y, rect.width(), lineH), Qt::AlignLeft | Qt::AlignVCenter, line.trimmed());
        y += lineH;
    }
    painter->restore();
    return qRound(y - rect.top());
}

} // namespace

int CardRenderUtils::webSpineWidth(int scale) {
    return qMax(30, 46 * scale / 100);
}

void CardRenderUtils::paintWebAddress(QPainter *painter, const CardBodyContext &ctx, const QUrl &url,
                                      const QString &title, const QPixmap &favicon) {
    auto px = [&](int v) { return qMax(1, v * ctx.scale / 100); };
    auto font = [&](qreal points, QFont::Weight weight) {
        QFont f = painter->font();
        applyUiFontDefaults(f);
        f.setPointSizeF(qMax(7.0, points * ctx.scale / 100));
        f.setWeight(weight);
        return f;
    };
    const QString host = displayHost(url);
    const QColor color = siteColor(host, favicon);
    const WebPathSummary path = summarizeWebPath(url);
    const bool hasTitle = !title.trimmed().isEmpty() && !titleNamesOnlySite(title, host);
    // The page: its title, or what its path says, or for the site itself its name.
    const QString headline = hasTitle ? title.simplified() : (!path.headline.isEmpty() ? path.headline : host);
    const QString context = hasTitle ? QString() : path.context;

    // A band in the site's colour down the left, like a book's spine, fading in
    // below the header and out at the foot. Edge to edge when the card gives
    // the body its full width (Aurora), else a rounded band inside it.
    const bool fullBleed = ctx.previewRect == ctx.bodyRect;
    const QRect spine = fullBleed
        ? QRect(ctx.bodyRect.left(), ctx.bodyRect.top(), webSpineWidth(ctx.scale), ctx.bodyRect.height())
        : QRect(ctx.bodyRect.left() + px(10), ctx.bodyRect.top() + px(8), webSpineWidth(ctx.scale) - px(6),
                ctx.bodyRect.height() - px(14));
    {
        const QColor top = QColor::fromRgbF(color.redF() * 0.85, color.greenF() * 0.85, color.blueF() * 0.85);
        const QColor mid = QColor::fromRgbF(color.redF() * 0.70, color.greenF() * 0.70, color.blueF() * 0.70);
        QColor clear = top;
        clear.setAlpha(0);
        QColor foot = color;
        foot.setAlpha(64);
        QLinearGradient band(spine.topLeft(), spine.bottomLeft());
        band.setColorAt(0.0, clear);
        band.setColorAt(qMin(0.4, px(22) / qMax(1.0, qreal(spine.height()))), top);
        band.setColorAt(0.6, mid);
        band.setColorAt(1.0, foot);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(band);
        if (fullBleed) {
            painter->drawRect(spine);
        } else {
            painter->drawRoundedRect(QRectF(spine), px(10), px(10));
        }
        painter->restore();
    }

    // In it the site's icon on a white tile, or its first letter in its colour,
    // and below that its name sideways, read from the top, fading out.
    const int tileSize = px(26);
    const QRect tile(spine.center().x() - tileSize / 2 + 1, spine.top() + px(10), tileSize, tileSize);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, 245));
    painter->drawRoundedRect(QRectF(tile), px(8), px(8));
    if (!favicon.isNull()) {
        const int inset = qMax(2, tileSize * 11 / 100);
        painter->drawPixmap(tile.adjusted(inset, inset, -inset, -inset), favicon);
    } else {
        painter->setFont(font(10.5, QFont::ExtraBold));
        painter->setPen(color);
        painter->drawText(tile, Qt::AlignCenter, host.left(1).toUpper());
    }
    painter->restore();

    const QString name = siteName(host);
    if (!name.isEmpty()) {
        const QFont nameFont = font(16.5, QFont::Black);
        const QFontMetrics nameMetrics(nameFont, painter->device());
        const int nameTop = tile.bottom() + 1 + px(10);
        const int run = spine.bottom() - nameTop - px(4);
        if (run > nameMetrics.height()) {
            painter->save();
            painter->setClipRect(spine);
            painter->translate(spine.center().x() + nameMetrics.height() / 2.0, nameTop);
            painter->rotate(90);
            // Turned, x runs down the band and y across it, from its right edge.
            const QRectF line(0, 0, run, nameMetrics.height());
            QLinearGradient fade(line.topLeft(), line.topRight());
            fade.setColorAt(0.0, QColor(255, 255, 255, 230));
            fade.setColorAt(0.7, QColor(255, 255, 255, 230));
            fade.setColorAt(1.0, QColor(255, 255, 255, 0));
            painter->setFont(nameFont);
            painter->setPen(QPen(QBrush(fade), 1));
            painter->drawText(line, Qt::AlignLeft | Qt::AlignVCenter, name);
            painter->restore();
        }
    }

    // Beside it: the site in its colour, where on it, then the page.
    const int left = spine.right() + 1 + px(fullBleed ? 14 : 10);
    const int right = ctx.bodyRect.right() - px(fullBleed ? 14 : 12);
    const int width = qMax(0, right - left + 1);
    // Aurora's body runs to the card's foot, with its footer line below the text.
    const int bottom = fullBleed ? ctx.bodyRect.bottom() - px(34) : ctx.bodyRect.bottom() - px(6);
    int y = ctx.bodyRect.top() + px(8);
    {
        const QColor hsl = color.toHsl();
        const QColor soft = QColor::fromHslF(qMax<qreal>(0.0, hsl.hslHueF()), qMin<qreal>(1.0, hsl.hslSaturationF() + 0.1),
                                             ctx.darkTheme ? 0.72 : 0.38);
        const QFont siteFont = font(8.5, QFont::DemiBold);
        const int h = QFontMetrics(siteFont, painter->device()).height() + px(1);
        drawElidedText(painter, QRect(left, y, width, h), host, siteFont, soft, Qt::AlignLeft | Qt::AlignVCenter);
        y += h + px(3);
    }
    if (!context.isEmpty()) {
        const QFont contextFont = font(8.5, QFont::Normal);
        const int h = QFontMetrics(contextFont, painter->device()).height() + px(1);
        drawElidedText(painter, QRect(left, y, width, h), context, contextFont, ctx.theme.footerTextColor,
                       Qt::AlignLeft | Qt::AlignVCenter, Qt::ElideMiddle);
        y += h + px(3);
    }
    drawClampedText(painter, QRect(left, y + px(2), width, qMax(0, bottom - y - px(2))), headline,
                    font(13.5, QFont::ExtraBold), ctx.bodyTextColor, 4, 1.16);
}

bool CardRenderUtils::looksLikeCode(const QString &text) {
    if (text.size() < 10 || text.size() > 50000) {
        return false;
    }

    // Sample the first ~2000 chars for speed.
    const QString sample = text.left(2000);
    const QStringList lines = sample.split(QLatin1Char('\n'));
    if (lines.size() < 2) {
        // A one-line shell command. Words that are also English need a flag, path or pipe too.
        static const QRegularExpression shellCommand(QStringLiteral(
            "^\\s*(?:\\$\\s+)?(?:"
            "(?:sudo|git|npm|npx|pnpm|yarn|pip3?|python3?|docker|kubectl|apt|apt-get|dnf|yum|pacman|brew|cmake|cargo|"
            "curl|wget|ssh|scp|rsync|chmod|chown|mkdir|systemctl|journalctl)\\s+\\S"
            "|(?:cd|ls|cp|mv|rm|cat|grep|find|make|go|node|tar|echo|export)\\s+.*(?:\\s--?\\w|/|\\||&&|=)"
            "|\\./\\S+)"));
        return sample.size() <= 300 && shellCommand.match(sample).hasMatch();
    }

    int codeSignals = 0;

    // 1. Leading indentation (spaces/tabs) on multiple lines.
    int indentedLines = 0;
    for (const QString &line : lines) {
        if (!line.isEmpty() && (line[0] == QLatin1Char(' ') || line[0] == QLatin1Char('\t'))) {
            ++indentedLines;
        }
    }
    if (indentedLines * 3 >= lines.size()) {
        ++codeSignals; // >=33% lines indented
    }

    // 2. Common syntax patterns.
    static const QStringList patterns = {
        QStringLiteral("function "), QStringLiteral("def "),
        QStringLiteral("class "), QStringLiteral("import "),
        QStringLiteral("const "), QStringLiteral("let "),
        QStringLiteral("var "), QStringLiteral("return "),
        QStringLiteral("if ("), QStringLiteral("if("),
        QStringLiteral("for ("), QStringLiteral("for("),
        QStringLiteral("while ("), QStringLiteral("while("),
        QStringLiteral("switch ("), QStringLiteral("switch("),
        QStringLiteral("#include"), QStringLiteral("#define"),
        QStringLiteral("#pragma"), QStringLiteral("#ifndef"),
        QStringLiteral("public:"), QStringLiteral("private:"),
        QStringLiteral("protected:"), QStringLiteral("namespace "),
        QStringLiteral("package "), QStringLiteral("struct "),
        QStringLiteral("enum "), QStringLiteral("interface "),
        QStringLiteral("impl "), QStringLiteral("fn "),
        QStringLiteral("pub fn"), QStringLiteral("async "),
        QStringLiteral("await "), QStringLiteral("=> {"),
        QStringLiteral("-> {"), QStringLiteral("SELECT "),
        QStringLiteral("FROM "), QStringLiteral("WHERE "),
        QStringLiteral("CREATE TABLE"),
    };
    int patternHits = 0;
    for (const QString &pat : patterns) {
        if (sample.contains(pat)) {
            ++patternHits;
        }
    }
    if (patternHits >= 2) {
        codeSignals += 2;
    } else if (patternHits >= 1) {
        ++codeSignals;
    }

    // 3. Bracket/brace density.
    int braces = 0;
    for (const QChar &ch : sample) {
        if (ch == QLatin1Char('{') || ch == QLatin1Char('}')
            || ch == QLatin1Char('[') || ch == QLatin1Char(']')) {
            ++braces;
        }
    }
    if (braces >= 4) {
        ++codeSignals;
    }

    // 4. Semicolons at line endings.
    int semiLines = 0;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.endsWith(QLatin1Char(';'))) {
            ++semiLines;
        }
    }
    if (semiLines * 4 >= lines.size()) {
        ++codeSignals; // >=25% lines end with ;
    }

    return codeSignals >= 2;
}
