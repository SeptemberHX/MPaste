#include "CardBodyRenderer.h"

#include <QApplication>
#include <QCache>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QTextLayout>
#include <QObject>
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
    static const QString family = chooseInstalledFontFamily(
        {
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Microsoft YaHei"),
            QStringLiteral("Segoe UI"),
            QStringLiteral("Noto Sans"),
            QStringLiteral("Arial")
        },
        QApplication::font().family());
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

    if (lineSpacing <= 0) {
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    } else {
        QTextLayout layout(text, font, painter->device());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);

        const QFontMetricsF fm(font);
        const qreal lineH = fm.height() * lineSpacing;

        layout.beginLayout();
        qreal y = 0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid() || y + lineH > rect.height()) break;
            line.setLineWidth(rect.width());
            line.setPosition(QPointF(0, y));
            y += lineH;
        }
        layout.endLayout();
        layout.draw(painter, rect.topLeft());
    }

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
            return QObject::tr("Office shape preview");
        case Text:
        case All:
            break;
    }
    return card.normalizedText.trimmed();
}

bool CardRenderUtils::looksLikeCode(const QString &text) {
    if (text.size() < 10 || text.size() > 50000) {
        return false;
    }

    // Sample the first ~2000 chars for speed.
    const QString sample = text.left(2000);
    const QStringList lines = sample.split(QLatin1Char('\n'));
    if (lines.size() < 2) {
        return false;
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
