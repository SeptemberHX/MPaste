// input: Depends on PosterCard.h, CardRenderUtils, ColorCardBody, LinkCardBody and the delegate's thumbnail cache.
// output: Implements the Poster card style's panel (the card below its header; the delegate draws the header and the caption pill): each kind of content, and each case of it, set as a poster of its own.
// pos: Widget-layer card painter, used by ClipboardCardDelegate when the card style is Poster.
// update: If I change, update this header block and my folder README.md.
#include "PosterCard.h"

#include "CardBodyRenderer.h"
#include "ColorCardBody.h"
#include "LinkCardBody.h"
#include "utils/MxGraphRenderer.h"
#include "widget/ClipboardBoardModel.h"
#include "widget/ClipboardCardDelegate.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <initializer_list>
#include <cmath>
#include <QTextLine>
#include <QHash>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QSet>
#include <QTextLayout>
#include <QTextOption>

namespace {

// The panel a poster fills, and the card's measures at this scale.
struct Frame {
    QPainter *painter;
    QRect panel;
    int scale;
    int px(int value) const { return qMax(1, value * scale / 100); }
    int pad() const { return px(14); }
    // On the card's text margins, a little clear of the panel's top, and of
    // the caption pill the delegate lays over its foot.
    QRect content() const { return panel.adjusted(pad(), px(9), -pad(), -px(34)); }
};

QFont uiFont(const Frame &f, qreal points, QFont::Weight weight) {
    QFont font = f.painter->font();
    CardRenderUtils::applyUiFontDefaults(font);
    font.setPointSizeF(points * f.scale / 100);
    font.setWeight(weight);
    return font;
}

QFont monoFont(const Frame &f, qreal points, QFont::Weight weight) {
    QFont font = f.painter->font();
    CardRenderUtils::applyMonoFontDefaults(font);
    font.setPointSizeF(points * f.scale / 100);
    font.setWeight(weight);
    return font;
}

// Mostly Latin letters: a face's italic suits it; Chinese set in a slanted
// face only looks pushed over.
bool mostlyLatin(const QString &text) {
    int latin = 0;
    int other = 0;
    for (const QChar ch : text) {
        if (!ch.isLetter()) {
            continue;
        }
        (ch.script() == QChar::Script_Latin ? latin : other) += 1;
    }
    return latin > other;
}

QFont serifFont(const Frame &f, qreal points, bool italic) {
    QFont font = f.painter->font();
    font.setFamilies({QStringLiteral("Noto Serif CJK SC"), QStringLiteral("Source Han Serif SC"),
                      QStringLiteral("Songti SC"), QStringLiteral("Georgia"), QStringLiteral("SimSun"),
                      QStringLiteral("serif")});
    font.setStyleHint(QFont::Serif, QFont::PreferDefault);
    font.setPointSizeF(points * f.scale / 100);
    font.setWeight(QFont::Medium);
    font.setItalic(italic);
    return font;
}

struct Laid {
    qreal height = 0;       // of the lines that fit
    QPointF end;            // where the last one stops, in the painter's coordinates
    bool all = true;        // every line fitted
};

// The text wrapped into `rect`, its own line breaks kept; lines past the foot
// are left out. Drawn when `painter` is set, only measured otherwise.
Laid layoutText(QPainter *painter, QPaintDevice *device, const QRectF &rect, const QString &text,
                const QFont &font, const QColor &color, qreal spacing) {
    Laid laid;
    if (text.isEmpty() || rect.width() <= 0) {
        return laid;
    }
    // QTextLayout breaks lines only at U+2028, not at '\n'.
    QString lines = text;
    lines.replace(QLatin1String("\r\n"), QString(QChar::LineSeparator));
    lines.replace(QLatin1Char('\n'), QChar::LineSeparator);
    QTextLayout layout(lines, font, device);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    const qreal minLine = QFontMetricsF(font, device).height() * spacing;
    layout.beginLayout();
    qreal y = 0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(rect.width());
        const qreal height = qMax(line.height() * spacing, minLine);
        if (y + height > rect.height() + 0.5) {
            line.setPosition(QPointF(0, rect.height() + 9999));
            laid.all = false;
            break;
        }
        line.setPosition(QPointF(0, y));
        laid.end = rect.topLeft() + QPointF(line.naturalTextWidth(), y);
        laid.height = y + height;
        y += height;
    }
    layout.endLayout();
    if (painter) {
        painter->save();
        painter->setPen(color);
        layout.draw(painter, rect.topLeft());
        painter->restore();
    }
    return laid;
}

// The largest size from `maxPoints` down to `minPoints` at which the whole
// text fits the box; `minPoints` when none does.
QFont fitFont(const Frame &f, QFont font, const QString &text, const QSizeF &box, qreal maxPoints,
              qreal minPoints, qreal spacing) {
    for (qreal points = maxPoints; points > minPoints; points -= 1.0) {
        font.setPointSizeF(points * f.scale / 100);
        const Laid laid = layoutText(nullptr, f.painter->device(), QRectF(QPointF(0, 0), box), text, font, QColor(), spacing);
        if (laid.all) {
            return font;
        }
    }
    font.setPointSizeF(minPoints * f.scale / 100);
    return font;
}

// Letters as a shape: as tall as `box`, set a little narrow, and no wider
// than it. `alignRight` hangs them from the box's right edge.
void drawGiant(QPainter *painter, const QString &text, const QRectF &box, const QColor &color,
               bool alignRight = false, bool serif = false) {
    if (text.isEmpty() || box.isEmpty()) {
        return;
    }
    QFont font = painter->font();
    if (serif) {
        font.setFamilies({QStringLiteral("Georgia"), QStringLiteral("Noto Serif"), QStringLiteral("serif")});
        font.setStyleHint(QFont::Serif, QFont::PreferDefault);
        font.setItalic(true);
        font.setWeight(QFont::Bold);
    } else {
        CardRenderUtils::applyUiFontDefaults(font);
        font.setWeight(QFont::Black);
    }
    font.setPixelSize(200);
    QPainterPath glyphs;
    glyphs.addText(0, 0, font, text);
    const QRectF bounds = glyphs.boundingRect();
    if (bounds.isEmpty()) {
        return;
    }
    const qreal sy = box.height() / bounds.height();
    const qreal sx = qMin(sy * (serif ? 1.0 : 0.86), box.width() / bounds.width());
    QTransform place;
    place.translate(alignRight ? box.right() - bounds.width() * sx : box.left(), box.top());
    place.scale(sx, sy);
    place.translate(-bounds.left(), -bounds.top());
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->fillPath(place.map(glyphs), color);
    painter->restore();
}

// A shade over the panel's foot, clear `reach` pixels up, under the words:
// dark under white words, or paper-white under dark ones where the picture
// itself is light there (a dark shade on a white page only turns it grey).
void drawFootShade(const Frame &f, int reach, int strength, bool light) {
    const QRect band(f.panel.left(), f.panel.bottom() - reach + 1, f.panel.width(), reach);
    auto tone = [light](int alpha) { return light ? QColor(250, 250, 252, alpha) : QColor(10, 12, 20, alpha); };
    QLinearGradient shade(band.topLeft(), band.bottomLeft());
    shade.setColorAt(1.0, tone(strength));
    shade.setColorAt(0.5, tone(strength * 62 / 100));
    shade.setColorAt(0.0, tone(0));
    f.painter->fillRect(band, shade);
}

// Whether the picture is light along its top or its foot (the quarter there).
bool edgeIsLight(const QPixmap &picture, bool top) {
    if (picture.isNull()) {
        return false;
    }
    const QImage small = picture.toImage().scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_ARGB32);
    qreal sum = 0;
    int count = 0;
    for (int y = top ? 0 : 12; y < (top ? 4 : 16); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            const QColor c = QColor::fromRgba(small.pixel(x, y));
            sum += 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
            ++count;
        }
    }
    return count > 0 && sum / count > 0.62;
}

QColor wordInk(bool onLight, int alpha) {
    return onLight ? QColor(20, 22, 28, alpha) : QColor(255, 255, 255, alpha);
}

// The first letter or digit of the text, upper case: what runs off a poster's corner.
QString initialOf(const QString &text) {
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber()) {
            return QString(ch.toUpper());
        }
    }
    return {};
}

// A field of one hue falling to a deeper tone of it in the far corner.
void fillField(const Frame &f, qreal hue, qreal saturation, qreal light, qreal fall) {
    QLinearGradient ground(f.panel.topLeft(), f.panel.bottomRight());
    ground.setColorAt(0.0, QColor::fromHslF(hue, saturation, light));
    ground.setColorAt(0.6, QColor::fromHslF(hue, saturation, light - fall * 0.35));
    ground.setColorAt(1.0, QColor::fromHslF(hue, saturation * 0.9, qMax<qreal>(0.08, light - fall)));
    f.painter->fillRect(f.panel, ground);
}

void drawCornerLetter(const Frame &f, const QString &letter, const QColor &color) {
    drawGiant(f.painter, letter, QRectF(f.panel.left() + f.panel.width() * 0.34, f.panel.top() + f.panel.height() * 0.16,
                                        f.panel.width() * 0.62, f.panel.height() * 1.04),
              color, true);
}

// A soft shadow under a sheet lying on the panel.
void drawSheetShadow(QPainter *painter, const QRectF &sheet, int strength) {
    painter->save();
    painter->setPen(Qt::NoPen);
    for (int i = 4; i >= 1; --i) {
        painter->setBrush(QColor(20, 14, 6, strength / (i + 1)));
        painter->drawRoundedRect(sheet.adjusted(-i * 1.5, i, i * 1.5, i * 3.0), 3 + i, 3 + i);
    }
    painter->restore();
}

// Ruled squares across the panel, as on graph or drafting paper.
void drawGrid(const Frame &f, int step, const QColor &line) {
    for (int x = f.panel.left() + step; x < f.panel.right(); x += step) {
        f.painter->fillRect(QRect(x, f.panel.top(), 1, f.panel.height()), line);
    }
    for (int y = f.panel.top() + step; y < f.panel.bottom(); y += step) {
        f.painter->fillRect(QRect(f.panel.left(), y, f.panel.width(), 1), line);
    }
}

// The picture as wide as `page` and from its top; what runs past the foot is cut by the caller's clip.
void drawFromTop(QPainter *painter, const QRectF &page, const QPixmap &picture) {
    if (picture.isNull() || picture.width() <= 0) {
        return;
    }
    const qreal height = page.width() * picture.height() / picture.width();
    painter->drawPixmap(QRectF(page.left(), page.top(), page.width(), height), picture, QRectF(picture.rect()));
}

bool pictureReady(const CardData &card) {
    return !card.thumbnail.isNull()
        && (card.previewState == ClipboardBoardModel::PreviewReady
            || card.previewState == ClipboardBoardModel::PreviewNotApplicable);
}

// A small label with a filled ground; returns its width.
int drawChip(const Frame &f, int x, int y, const QString &text, const QColor &ground, const QColor &ink, const QFont &font,
             bool flagLeft = false) {
    const QFontMetrics metrics(font, f.painter->device());
    const int height = metrics.height() + f.px(4);
    const int width = metrics.horizontalAdvance(text) + f.px(flagLeft ? 20 : 10);
    f.painter->save();
    f.painter->setRenderHint(QPainter::Antialiasing, true);
    f.painter->setPen(Qt::NoPen);
    f.painter->setBrush(ground);
    if (flagLeft) {
        // Hung from the panel's left edge: square there, round on the right.
        f.painter->setClipRect(QRect(x, y, width, height));
        f.painter->drawRoundedRect(QRectF(x - f.px(8), y, width + f.px(8), height), f.px(5), f.px(5));
    } else {
        f.painter->drawRoundedRect(QRectF(x, y, width, height), f.px(4), f.px(4));
    }
    f.painter->restore();
    CardRenderUtils::drawElidedText(f.painter, QRect(x + f.px(flagLeft ? 12 : 5), y, width - f.px(flagLeft ? 18 : 8), height),
                                    text, font, ink, Qt::AlignLeft | Qt::AlignVCenter);
    return width;
}

// --- the posters ---

// Code: a phosphor screen, scan lines across it and a cursor after the last character.
void paintTerminal(const Frame &f, const QString &source) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#04140C")));
    const QColor phosphor(QStringLiteral("#6DFFB0"));
    QColor line = phosphor;
    line.setAlpha(16);
    const int step = f.px(4);
    for (int y = f.panel.top(); y <= f.panel.bottom(); y += step) {
        p->fillRect(QRect(f.panel.left(), y, f.panel.width(), 1), line);
    }
    const QRect content = f.content();
    // One command gets its prompt.
    const bool command = !source.contains(QLatin1Char('\n')) && !source.startsWith(QLatin1Char('$'));
    const QString text = command ? QStringLiteral("$ ") + source : source;
    const QFont font = fitFont(f, monoFont(f, 12, QFont::Bold), text, content.size(), 12.5, 8.5, 1.3);
    // A soft bloom around the letters, as a tube's glow.
    QColor bloom = phosphor;
    bloom.setAlpha(34);
    for (const QPointF &offset : {QPointF(-1.2, 0), QPointF(1.2, 0), QPointF(0, -1.2), QPointF(0, 1.2)}) {
        layoutText(p, p->device(), QRectF(content).translated(offset), text, font, bloom, 1.3);
    }
    const Laid laid = layoutText(p, p->device(), QRectF(content), text, font, phosphor, 1.3);
    if (command) {
        // The prompt dimmer than what was typed: painted over in the screen's own dark green.
        const QFontMetricsF metrics(font, p->device());
        p->fillRect(QRectF(content.left() - 2, content.top(), metrics.horizontalAdvance(QStringLiteral("$ ")) + 1,
                           metrics.height() * 1.3), QColor(QStringLiteral("#04140C")));
        layoutText(p, p->device(), QRectF(content), QStringLiteral("$"), font, QColor(QStringLiteral("#2F8F5D")), 1.3);
    }
    if (laid.all && laid.height > 0) {
        const QFontMetricsF metrics(font, p->device());
        const QRectF cursor(laid.end.x() + f.px(3), laid.end.y() + metrics.height() * 0.14,
                            metrics.averageCharWidth(), metrics.height() * 0.86);
        if (cursor.right() <= content.right()) {
            p->fillRect(cursor, phosphor);
        }
    }
}

// Plain words: a type poster, built as a link's cover is. A field of the
// app's colour (paper, for an app without one), the words in heavy ink as
// large as they fit, their first character behind them in the corner. The
// colour is held back from full strength: a row of these is a set of
// prints, not a paint box.
void paintWords(const Frame &f, const QString &text, const QColor &appTone) {
    QPainter *p = f.painter;
    const QColor hsl = appTone.toHsl();
    const bool coloured = hsl.hslSaturationF() >= 0.08;
    const qreal hue = qMax<qreal>(0.0, hsl.hslHueF());
    // Greens and cyans glare at a lightness reds carry well: set them deeper.
    const qreal degrees = hue * 360.0;
    const bool glaring = degrees > 70.0 && degrees < 200.0;
    // Blues and violets are weak under black ink at any strength: they go deep, under light ink.
    const bool deep = coloured && degrees >= 215.0 && degrees <= 300.0;
    if (deep) {
        fillField(f, hue, qBound<qreal>(0.40, hsl.hslSaturationF(), 0.55), 0.36, 0.18);
    } else if (coloured) {
        fillField(f, hue, qBound<qreal>(0.40, hsl.hslSaturationF(), glaring ? 0.46 : 0.62), glaring ? 0.52 : 0.60, 0.24);
    } else {
        QLinearGradient paper(f.panel.topLeft(), f.panel.bottomRight());
        paper.setColorAt(0.0, QColor(QStringLiteral("#F4EDDF")));
        paper.setColorAt(0.6, QColor(QStringLiteral("#E7DCC6")));
        paper.setColorAt(1.0, QColor(QStringLiteral("#B9AB8E")));
        p->fillRect(f.panel, paper);
    }
    const QColor ink = deep ? QColor(244, 239, 228, 250) : QColor(18, 15, 10, 240);
    const QString shown = text.left(600).trimmed();
    drawCornerLetter(f, initialOf(shown), deep ? QColor(255, 255, 255, 22) : QColor(18, 15, 10, coloured ? 44 : 30));

    const QRect content = f.content();
    // A few words are shouted; a paragraph is set to be read.
    const int length = shown.size();
    QFont font = uiFont(f, 12, length <= 40 ? QFont::Black : (length <= 110 ? QFont::Bold : QFont::DemiBold));
    font.setStretch(length <= 40 ? 90 : 100);
    const qreal spacing = length <= 40 ? 1.08 : 1.22;
    font = fitFont(f, font, shown, content.size(), 28, 9.5, spacing);
    layoutText(p, p->device(), QRectF(content), shown, font, ink, spacing);
}

// A key, a hash, an id: one long run of ASCII with no words in it. Set large
// it only shouts noise, so it gets a quiet poster of its own.
bool looksLikeToken(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.size() < 16) {
        return false;
    }
    int run = 0;
    int longest = 0;
    bool digit = false;
    for (const QChar ch : trimmed) {
        if (ch.unicode() >= 0x80) {
            return false;
        }
        if (ch.isSpace()) {
            run = 0;
            continue;
        }
        digit = digit || ch.isDigit();
        longest = qMax(longest, ++run);
    }
    return longest >= 16 && digit;
}

// Rich text: a pull quote on paper, a large mark behind it.
void paintQuote(const Frame &f, const QString &text) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#F3ECDF")));
    drawGiant(p, QStringLiteral("\u201C"),
              QRectF(f.panel.left() + f.px(8), f.panel.top() + f.px(6), f.px(92), f.px(72)),
              QColor(123, 92, 255, 60), false, true);
    const QRect content = f.content();
    const QString shown = text.left(600).trimmed();
    const QFont font = fitFont(f, serifFont(f, 12, mostlyLatin(shown)), shown, content.size(), 15.5, 9.5, 1.22);
    layoutText(p, p->device(), QRectF(content), shown, font, QColor(QStringLiteral("#1B1721")), 1.22);
}

// A link without a picture: a cover in the site's colour, its initial run off the corner.
void paintCover(const Frame &f, const QUrl &url, const CardData &card) {
    QPainter *p = f.painter;
    const QString host = CardRenderUtils::displayHost(url);
    const QColor site = CardRenderUtils::webSiteColor(url, card.favicon).toHsl();
    fillField(f, qMax<qreal>(0.0, site.hslHueF()), qMin<qreal>(site.hslSaturationF(), 0.70), 0.60, 0.42);
    const QColor ink(18, 15, 10, 240);
    drawCornerLetter(f, initialOf(host), QColor(18, 15, 10, 46));

    QString context;
    const QString headline = CardRenderUtils::webHeadline(url, card.title, &context);
    const QRect content = f.content();
    QFont kicker = uiFont(f, 7.5, QFont::Black);
    kicker.setLetterSpacing(QFont::PercentageSpacing, 118);
    const int kickerHeight = f.px(16);
    const QString kickerText = context.isEmpty() ? host.toUpper() : host.toUpper() + QStringLiteral(" \u00B7 ") + context.toUpper();
    CardRenderUtils::drawElidedText(p, QRect(content.left(), content.top(), content.width(), kickerHeight), kickerText,
                                    kicker, ink, Qt::AlignLeft | Qt::AlignVCenter);
    const QRect titleRect = content.adjusted(0, kickerHeight + f.px(2), 0, 0);
    // A short Latin name is shouted; a sentence or Chinese keeps its case.
    const QString shown = headline.size() <= 14 && mostlyLatin(headline) ? headline.toUpper() : headline;
    QFont titleFont = uiFont(f, 12, QFont::Black);
    titleFont.setStretch(90);
    titleFont = fitFont(f, titleFont, shown, titleRect.size(), 28, 11, 1.08);
    layoutText(p, p->device(), QRectF(titleRect), shown, titleFont, ink, 1.08);
}

// --- plain text, by what it is ---

// A figure: a number with at most a short unit ("2.8B", "42%", "¥3,200").
bool looksLikeFigure(const QString &text) {
    const QString t = text.trimmed();
    if (t.isEmpty() || t.size() > 12 || t.contains(QLatin1Char('\n'))) {
        return false;
    }
    int digits = 0;
    int letters = 0;
    for (const QChar ch : t) {
        if (ch.isDigit()) {
            ++digits;
        } else if (ch.isLetter()) {
            ++letters;
        } else if (!QStringLiteral(".,%+-:/ $¥€£~").contains(ch)) {
            return false;
        }
    }
    return digits >= 1 && letters <= 3 && digits >= letters;
}

// A path: /a/b/c, ~/a/b or C:\a\b on one line.
bool looksLikePath(const QString &text) {
    const QString t = text.trimmed();
    if (t.size() < 4 || t.size() > 400 || t.contains(QLatin1Char('\n'))) {
        return false;
    }
    const bool rooted = t.startsWith(QLatin1Char('/')) || t.startsWith(QLatin1String("~/"))
        || (t.size() > 2 && t.at(0).isLetter() && t.at(1) == QLatin1Char(':') && (t.at(2) == QLatin1Char('\\') || t.at(2) == QLatin1Char('/')));
    return rooted && t.count(QLatin1Char('/')) + t.count(QLatin1Char('\\')) >= 2 && !t.startsWith(QLatin1String("//"));
}

// A list: two to six short lines.
QStringList shortLines(const QString &text) {
    QStringList lines;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.size() > 26) {
            return {};
        }
        lines << line;
    }
    return lines.size() >= 2 && lines.size() <= 6 ? lines : QStringList();
}

// A figure fills the panel: the number in ink, its unit in red, on paper.
void paintFigure(const Frame &f, const QString &text) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#F4EFE4")));
    const QString t = text.trimmed();
    int split = t.size();
    while (split > 0 && (t.at(split - 1).isLetter() || t.at(split - 1) == QLatin1Char('%'))) {
        --split;
    }
    const QString number = split > 0 ? t.left(split) : t;
    const QString unit = split > 0 ? t.mid(split) : QString();
    const QRect content = f.content();
    p->fillRect(QRect(content.left(), content.top() + f.px(1), f.px(34), f.px(4)), QColor(QStringLiteral("#D9241B")));

    QFont font = p->font();
    CardRenderUtils::applyUiFontDefaults(font);
    font.setWeight(QFont::Black);
    font.setPixelSize(200);
    QPainterPath numberPath;
    numberPath.addText(0, 0, font, number);
    QPainterPath unitPath;
    unitPath.addText(QFontMetricsF(font).horizontalAdvance(number), 0, font, unit);
    const QRectF bounds = numberPath.boundingRect().united(unitPath.boundingRect());
    if (bounds.isEmpty()) {
        return;
    }
    const QRectF box = QRectF(content).adjusted(0, f.px(12), 0, 0);
    const qreal scale = qMin(box.width() / (bounds.width() * 0.9), box.height() / bounds.height());
    QTransform place;
    place.translate(box.left(), box.top());
    place.scale(scale * 0.9, scale);
    place.translate(-bounds.left(), -bounds.top());
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->fillPath(place.map(numberPath), QColor(18, 15, 10));
    p->fillPath(place.map(unitPath), QColor(QStringLiteral("#D9241B")));
    p->restore();
}

// A list: numbered rows on charcoal, a hairline between them.
void paintList(const Frame &f, const QStringList &lines) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#1E1F24")));
    const QRect content = f.panel.adjusted(f.pad(), f.px(5), -f.pad(), -f.px(30));
    const int row = qMin(f.px(30), content.height() / lines.size());
    const QFont numberFont = monoFont(f, 9.5, QFont::Bold);
    const QFont textFont = uiFont(f, row >= f.px(26) ? 11.5 : 10, QFont::Bold);
    const int numberWidth = QFontMetrics(numberFont, p->device()).horizontalAdvance(QStringLiteral("00")) + f.px(10);
    for (int i = 0; i < lines.size(); ++i) {
        const QRect line(content.left(), content.top() + i * row, content.width(), row);
        CardRenderUtils::drawElidedText(p, QRect(line.left(), line.top(), numberWidth, row),
                                        QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), numberFont,
                                        QColor(QStringLiteral("#E9B44C")), Qt::AlignLeft | Qt::AlignVCenter);
        CardRenderUtils::drawElidedText(p, line.adjusted(numberWidth, 0, 0, 0), lines.at(i), textFont,
                                        QColor(QStringLiteral("#F4EFE4")), Qt::AlignLeft | Qt::AlignVCenter);
        if (i + 1 < lines.size()) {
            p->fillRect(QRect(line.left(), line.bottom(), line.width(), 1), QColor(255, 255, 255, 26));
        }
    }
}

// A paragraph: set to be read, on paper, its first character dropped across three lines in the app's colour.
void paintParagraph(const Frame &f, const QString &text, const QColor &appTone) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#F4EFE4")));
    const QString shown = text.left(700).simplified();
    if (shown.isEmpty()) {
        return;
    }
    const QColor hsl = appTone.toHsl();
    const QColor capColor = hsl.hslSaturationF() < 0.08 ? QColor(QStringLiteral("#C0281F"))
        : QColor::fromHslF(qMax<qreal>(0.0, hsl.hslHueF()), qMin<qreal>(hsl.hslSaturationF(), 0.62), 0.33);
    const QRect content = f.content();
    const QColor ink(QStringLiteral("#1B1721"));
    const bool capital = shown.at(0).isLetterOrNumber();
    const QString body = capital ? shown.mid(1) : shown;

    for (const qreal points : {11.0, 10.0, 9.2, 8.6}) {
        const QFont font = serifFont(f, points, false);
        const QFontMetricsF metrics(font, p->device());
        const qreal lineHeight = metrics.height() * 1.28;
        // The capital: as tall as three lines, as wide as its own shape.
        QPainterPath cap;
        qreal indent = 0;
        const qreal capHeight = lineHeight * 2.0 + metrics.ascent();
        if (capital) {
            QFont capFont = font;
            capFont.setWeight(QFont::Bold);
            capFont.setPixelSize(200);
            QPainterPath shape;
            shape.addText(0, 0, capFont, shown.left(1));
            const QRectF bounds = shape.boundingRect();
            if (!bounds.isEmpty()) {
                const qreal scale = (capHeight * 0.92) / bounds.height();
                QTransform place;
                place.translate(content.left(), content.top() + metrics.ascent() * 0.16);
                place.scale(scale, scale);
                place.translate(-bounds.left(), -bounds.top());
                cap = place.map(shape);
                indent = bounds.width() * scale + f.px(7);
            }
        }
        QTextLayout layout(body, font, p->device());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        qreal y = 0;
        bool all = true;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) {
                break;
            }
            const bool beside = y < capHeight - lineHeight * 0.4;
            line.setLineWidth(content.width() - (beside ? indent : 0));
            if (y + lineHeight > content.height() + 0.5) {
                line.setPosition(QPointF(0, content.height() + 9999));
                all = false;
                break;
            }
            line.setPosition(QPointF(beside ? indent : 0, y));
            y += lineHeight;
        }
        layout.endLayout();
        if (!all && points > 8.6) {
            continue;   // smaller
        }
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillPath(cap, capColor);
        p->setPen(ink);
        layout.draw(p, content.topLeft());
        p->restore();
        return;
    }
}

// A key, a hash, an id: a stub. Its characters in fours, a perforation, and a bar code drawn from them.
void paintTicket(const Frame &f, const QString &text) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#EFE6D2")));
    const QColor ink(QStringLiteral("#1B1712"));
    const QColor tan(QStringLiteral("#8A7A5C"));
    QString key = text.trimmed();
    key.remove(QLatin1Char('\n'));
    key.remove(QLatin1Char(' '));
    const QRect content = f.content();
    QFont kicker = uiFont(f, 7.5, QFont::Black);
    kicker.setLetterSpacing(QFont::PercentageSpacing, 118);
    CardRenderUtils::drawElidedText(p, QRect(content.left(), content.top(), content.width(), f.px(14)),
                                    QStringLiteral("KEY \u00B7 %1").arg(key.size()), kicker, tan, Qt::AlignLeft | Qt::AlignVCenter);

    const QFont font = monoFont(f, 11.5, QFont::Bold);
    const QFontMetrics metrics(font, p->device());
    const int rowHeight = metrics.height() + f.px(2);
    const int columns = 4;
    const int rows = 3;
    const int columnWidth = content.width() / columns;
    const int groups = (key.size() + 3) / 4;
    const int top = content.top() + f.px(18);
    for (int i = 0; i < qMin(groups, columns * rows); ++i) {
        // More than fits: the last place says so.
        const bool more = i == columns * rows - 1 && groups > columns * rows;
        const QRect cell(content.left() + (i % columns) * columnWidth, top + (i / columns) * rowHeight, columnWidth, rowHeight);
        CardRenderUtils::drawElidedText(p, cell, more ? QStringLiteral("\u2026") : key.mid(i * 4, 4), font, ink,
                                        Qt::AlignLeft | Qt::AlignVCenter);
    }
    const int usedRows = qMin(rows, (groups + columns - 1) / columns);
    const int perforation = top + usedRows * rowHeight + f.px(8);
    p->save();
    QPen dash(QColor(QStringLiteral("#B9AB8E")), f.px(2), Qt::DashLine);
    p->setPen(dash);
    p->drawLine(f.panel.left(), perforation, f.panel.right(), perforation);
    p->restore();

    // The bar code: each byte of the key gives a bar and the gap after it.
    const QRect bars(content.left(), perforation + f.px(10), content.width(), qMax(0, content.bottom() - perforation - f.px(8)));
    const QByteArray bytes = key.toUtf8();
    if (bars.height() > f.px(8) && !bytes.isEmpty()) {
        int x = bars.left();
        for (int i = 0; x < bars.right(); ++i) {
            const uchar b = static_cast<uchar>(bytes.at(i % bytes.size())) + static_cast<uchar>(i / bytes.size() * 37);
            const int bar = f.px(1 + (b & 3));
            p->fillRect(QRect(x, bars.top(), qMin(bar, bars.right() - x + 1), bars.height()), ink);
            x += bar + f.px(1 + ((b >> 2) & 3));
        }
    }
}

// A path: each folder a step in, the last name large.
void paintPath(const Frame &f, const QString &text) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#16202E")));
    QString t = text.trimmed();
    t.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QStringList parts = t.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (t.startsWith(QLatin1Char('/'))) {
        parts.prepend(QString());   // the root, shown as "/"
    }
    if (parts.isEmpty()) {
        return;
    }
    const QString last = parts.takeLast();
    const int maxAbove = 4;
    const bool cut = parts.size() > maxAbove;
    while (parts.size() > maxAbove) {
        parts.removeFirst();
    }
    if (cut) {
        parts[0] = QStringLiteral("\u2026");
    }
    const QRect content = f.content();
    const QFont stepFont = monoFont(f, 9, QFont::Medium);
    const int stepHeight = QFontMetrics(stepFont, p->device()).height() + f.px(2);
    int y = content.top();
    int indent = 0;
    for (int i = 0; i < parts.size(); ++i) {
        indent = i * f.px(12);
        CardRenderUtils::drawElidedText(p, QRect(content.left() + indent, y, content.width() - indent, stepHeight),
                                        parts.at(i) + QStringLiteral(" /"), stepFont,
                                        i + 1 == parts.size() ? QColor(QStringLiteral("#8FA6C6")) : QColor(QStringLiteral("#6F86A6")),
                                        Qt::AlignLeft | Qt::AlignVCenter, Qt::ElideMiddle);
        y += stepHeight;
    }
    const QRect lastRect(content.left() + indent, y + f.px(2), content.width() - indent, qMax(f.px(20), content.bottom() - y - f.px(2)));
    const QFont lastFont = fitFont(f, monoFont(f, 12, QFont::Bold), last, lastRect.size(), 16, 9.5, 1.15);
    layoutText(p, p->device(), QRectF(lastRect), last, lastFont, QColor(QStringLiteral("#F4EFE4")), 1.15);
}

// --- code ---

// Several lines of code: an editor. A gutter of line numbers, keywords, strings, numbers and comments in their colours.
void paintEditor(const Frame &f, const QString &text) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#0F1424")));
    QString source = text;
    source.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    source.replace(QLatin1Char('\t'), QStringLiteral("    "));
    const QStringList lines = source.split(QLatin1Char('\n'));
    const QRect area = f.panel.adjusted(0, f.px(8), 0, -f.px(30));

    // As large as shows every line, down to a size that still reads.
    QFont font = monoFont(f, 9.5, QFont::Medium);
    qreal lineHeight = 0;
    for (const qreal points : {10.0, 9.2, 8.6, 8.0}) {
        font.setPointSizeF(points * f.scale / 100);
        lineHeight = QFontMetricsF(font, p->device()).height() * 1.32;
        if (lines.size() * lineHeight <= area.height()) {
            break;
        }
    }
    const QFontMetricsF metrics(font, p->device());
    const int gutter = qRound(metrics.horizontalAdvance(QStringLiteral("00")) + f.px(16));
    p->fillRect(QRect(f.panel.left(), f.panel.top(), gutter, f.panel.height()), QColor(QStringLiteral("#0B0F1C")));

    static const QSet<QString> keywords = {
        QStringLiteral("def"), QStringLiteral("return"), QStringLiteral("for"), QStringLiteral("in"), QStringLiteral("if"),
        QStringLiteral("else"), QStringLiteral("elif"), QStringLiteral("while"), QStringLiteral("class"), QStringLiteral("import"),
        QStringLiteral("from"), QStringLiteral("function"), QStringLiteral("const"), QStringLiteral("let"), QStringLiteral("var"),
        QStringLiteral("public"), QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("static"),
        QStringLiteral("void"), QStringLiteral("int"), QStringLiteral("bool"), QStringLiteral("auto"), QStringLiteral("struct"),
        QStringLiteral("enum"), QStringLiteral("namespace"), QStringLiteral("using"), QStringLiteral("new"), QStringLiteral("delete"),
        QStringLiteral("fn"), QStringLiteral("pub"), QStringLiteral("use"), QStringLiteral("impl"), QStringLiteral("match"),
        QStringLiteral("func"), QStringLiteral("package"), QStringLiteral("switch"), QStringLiteral("case"), QStringLiteral("break"),
        QStringLiteral("continue"), QStringLiteral("try"), QStringLiteral("catch"), QStringLiteral("except"), QStringLiteral("finally"),
        QStringLiteral("throw"), QStringLiteral("async"), QStringLiteral("await"), QStringLiteral("true"), QStringLiteral("false"),
        QStringLiteral("null"), QStringLiteral("None"), QStringLiteral("True"), QStringLiteral("False"), QStringLiteral("self"),
        QStringLiteral("this"), QStringLiteral("export"), QStringLiteral("default"), QStringLiteral("then"), QStringLiteral("fi"),
        QStringLiteral("do"), QStringLiteral("done"), QStringLiteral("echo"), QStringLiteral("select"), QStringLiteral("where"),
    };
    const QColor plain(QStringLiteral("#D7DCF0"));
    const QColor keywordColor(QStringLiteral("#FF8FB0"));
    const QColor stringColor(QStringLiteral("#A8E6A1"));
    const QColor numberColor(QStringLiteral("#FFD479"));
    const QColor commentColor(QStringLiteral("#6B7499"));
    auto format = [](int start, int length, const QColor &color) {
        QTextLayout::FormatRange range;
        range.start = start;
        range.length = length;
        range.format.setForeground(color);
        return range;
    };

    const QFont numberFont = font;
    qreal y = area.top();
    for (int n = 0; n < lines.size() && y + lineHeight <= area.bottom() + 1; ++n, y += lineHeight) {
        const QString &line = lines.at(n);
        CardRenderUtils::drawElidedText(p, QRect(f.panel.left(), qRound(y), gutter - f.px(8), qRound(lineHeight)),
                                        QString::number(n + 1), numberFont, QColor(QStringLiteral("#46507A")),
                                        Qt::AlignRight | Qt::AlignVCenter);
        QList<QTextLayout::FormatRange> ranges;
        int i = 0;
        while (i < line.size()) {
            const QChar ch = line.at(i);
            const bool hashComment = ch == QLatin1Char('#') && (i + 1 >= line.size() || line.at(i + 1).isSpace() || line.at(i + 1) == QLatin1Char('!'));
            if (hashComment || line.mid(i, 2) == QLatin1String("//")) {
                ranges << format(i, line.size() - i, commentColor);
                break;
            }
            if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
                int end = i + 1;
                while (end < line.size() && line.at(end) != ch) {
                    end += line.at(end) == QLatin1Char('\\') ? 2 : 1;
                }
                end = qMin(end + 1, line.size());
                ranges << format(i, end - i, stringColor);
                i = end;
                continue;
            }
            if (ch.isLetter() || ch == QLatin1Char('_')) {
                int end = i;
                while (end < line.size() && (line.at(end).isLetterOrNumber() || line.at(end) == QLatin1Char('_'))) {
                    ++end;
                }
                if (keywords.contains(line.mid(i, end - i))) {
                    ranges << format(i, end - i, keywordColor);
                }
                i = end;
                continue;
            }
            if (ch.isDigit()) {
                int end = i;
                while (end < line.size() && (line.at(end).isLetterOrNumber() || line.at(end) == QLatin1Char('.'))) {
                    ++end;
                }
                ranges << format(i, end - i, numberColor);
                i = end;
                continue;
            }
            ++i;
        }
        QTextLayout layout(line, font, p->device());
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(option);
        layout.setFormats(ranges);
        layout.beginLayout();
        QTextLine laid = layout.createLine();
        if (laid.isValid()) {
            laid.setLineWidth(100000);
            laid.setPosition(QPointF(0, 0));
        }
        layout.endLayout();
        p->save();
        p->setPen(plain);
        layout.draw(p, QPointF(f.panel.left() + gutter + f.px(8), y + (lineHeight - metrics.height()) / 2.0));
        p->restore();
    }
}

// --- rich text ---

// A heading over its text: an article. The heading heavy, a short rule, the text below.
bool paintArticle(const Frame &f, const QString &text) {
    QStringList lines;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        if (!raw.trimmed().isEmpty()) {
            lines << raw.trimmed();
        }
    }
    if (lines.size() < 2 || lines.first().size() > 28 || lines.first().size() < 2) {
        return false;
    }
    const QString heading = lines.takeFirst();
    const QString body = lines.join(QLatin1Char(' ')).left(500);
    if (body.size() < heading.size()) {
        return false;
    }
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#F3ECDF")));
    const QColor ink(QStringLiteral("#1B1721"));
    const QRect content = f.content();
    QFont headFont = serifFont(f, 12, false);
    headFont.setWeight(QFont::Black);
    const QRect headRect(content.left(), content.top(), content.width(), f.px(50));
    headFont = fitFont(f, headFont, heading, headRect.size(), 15.5, 11, 1.14);
    const Laid head = layoutText(p, p->device(), QRectF(headRect), heading, headFont, ink, 1.14);
    const int ruleTop = headRect.top() + qRound(head.height) + f.px(5);
    p->fillRect(QRect(content.left(), ruleTop, f.px(40), f.px(3)), ink);
    const QRect bodyRect(content.left(), ruleTop + f.px(10), content.width(), qMax(0, content.bottom() - ruleTop - f.px(10)));
    layoutText(p, p->device(), QRectF(bodyRect), body, serifFont(f, 9.2, false), QColor(QStringLiteral("#3A3442")), 1.3);
    return true;
}

// A table or laid-out page: as it was drawn, a sheet taped to the desk.
void paintTapedSheet(const Frame &f, const CardData &card, bool darkTheme) {
    QPainter *p = f.painter;
    // The picture's text is in the theme's ink: light on a dark sheet, dark on a white one.
    p->fillRect(f.panel, darkTheme ? QColor(QStringLiteral("#3A332B")) : QColor(QStringLiteral("#D9CFB9")));
    const QRectF sheet(f.panel.left() + f.px(16), f.panel.top() + f.px(16), f.panel.width() - f.px(32), f.panel.height());
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    p->translate(sheet.center().x(), sheet.top());
    p->rotate(-1.5);
    p->translate(-sheet.center().x(), -sheet.top());
    drawSheetShadow(p, sheet, darkTheme ? 150 : 90);
    p->fillRect(sheet, darkTheme ? QColor(QStringLiteral("#17181E")) : QColor(Qt::white));
    p->setClipRect(sheet, Qt::IntersectClip);
    drawFromTop(p, sheet.adjusted(f.px(6), f.px(6), -f.px(6), 0), card.thumbnail);
    p->restore();
    // The tape.
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->translate(f.panel.center().x(), f.panel.top() + f.px(14));
    p->rotate(-3);
    p->fillRect(QRectF(-f.px(24), -f.px(8), f.px(48), f.px(16)), QColor(255, 236, 150, 215));
    p->restore();
}

// A drawing (draw.io): pinned on a blueprint.
void paintBlueprint(const Frame &f, const CardData &card, int loadingPhase) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#1D4F91")));
    drawGrid(f, f.px(16), QColor(255, 255, 255, 26));
    const QRect frame = f.panel.adjusted(f.px(14), f.px(12), -f.px(14), -f.px(34));
    if (!pictureReady(card)) {
        CardRenderUtils::drawManagedVisualPreview(p, frame, card, f.scale, true, loadingPhase, true);
        return;
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    drawSheetShadow(p, QRectF(frame), 70);
    p->setPen(Qt::NoPen);
    p->setBrush(Qt::white);
    p->drawRoundedRect(QRectF(frame), f.px(4), f.px(4));
    p->restore();
    CardRenderUtils::drawContainPixmap(p, frame.adjusted(f.px(4), f.px(4), -f.px(4), -f.px(4)), card.thumbnail);
}

// --- pictures ---

// A photograph: edge to edge, nothing over it.
void paintPlate(const Frame &f, const CardData &card, int loadingPhase) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#0D0E12")));
    CardRenderUtils::drawManagedVisualPreview(p, f.panel, card, f.scale, true, loadingPhase, false);
}

// A long page (a screenshot of one, a scan): a sheet laid at a slight angle on a dark desk, read from its top.
void paintPageOnDesk(const Frame &f, const CardData &card) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#2B3A55")));
    const QRectF sheet(f.panel.left() + f.px(30), f.panel.top() + f.px(14), f.panel.width() - f.px(60), f.panel.height() * 1.3);
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    p->translate(sheet.center().x(), sheet.top());
    p->rotate(-2.0);
    p->translate(-sheet.center().x(), -sheet.top());
    drawSheetShadow(p, sheet, 150);
    p->fillRect(sheet, Qt::white);
    p->setClipRect(sheet, Qt::IntersectClip);
    drawFromTop(p, sheet, card.thumbnail);
    p->restore();
}

// A small picture (an icon, a sticker): at its own size, mounted on white card, not blown up.
void paintMounted(const Frame &f, const CardData &card) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#E7E0D2")));
    const QRect area = f.panel.adjusted(f.px(24), f.px(12), -f.px(24), -f.px(34));
    const int margin = f.px(10);
    QSizeF size = QSizeF(card.imageSize) / qMax<qreal>(1.0, card.thumbnail.devicePixelRatio());
    size = size.scaled(size.boundedTo(QSizeF(area.width() - margin * 2, area.height() - margin * 2)), Qt::KeepAspectRatio);
    const QRectF picture(area.center().x() - size.width() / 2.0, area.center().y() - size.height() / 2.0, size.width(), size.height());
    const QRectF mount = picture.adjusted(-margin, -margin, margin, margin);
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    drawSheetShadow(p, mount, 80);
    p->fillRect(mount, Qt::white);
    p->restore();
    CardRenderUtils::drawContainPixmap(p, picture.toRect(), card.thumbnail);
}

// An Office shape: whole, on squared paper.
void paintOnGraphPaper(const Frame &f, const CardData &card, int loadingPhase) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#F3ECDF")));
    drawGrid(f, f.px(14), QColor(27, 23, 18, 20));
    CardRenderUtils::drawManagedVisualPreview(p, f.panel.adjusted(f.px(14), f.px(10), -f.px(14), -f.px(34)), card, f.scale,
                                              false, loadingPhase, true);
}

void paintImage(const Frame &f, const CardData &card, int loadingPhase) {
    if (!pictureReady(card)) {
        paintPlate(f, card, loadingPhase);   // its placeholder
        return;
    }
    const QSize size = card.imageSize;
    if (size.isValid() && size.width() <= 220 && size.height() <= 150) {
        paintMounted(f, card);
    } else if (size.isValid() && size.height() > size.width() * 1.6) {
        paintPageOnDesk(f, card);
    } else {
        paintPlate(f, card, loadingPhase);
    }
}

// --- links ---

// A link with its page's picture: the picture, the site and the page named across its foot.
void paintLinkPicture(const Frame &f, const CardData &card, int loadingPhase, const QUrl &url) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#0D0E12")));
    CardRenderUtils::drawManagedVisualPreview(p, f.panel, card, f.scale, true, loadingPhase, false);
    const QString headline = CardRenderUtils::webHeadline(url, card.title);
    const bool lightFoot = pictureReady(card) && edgeIsLight(card.thumbnail, false);
    drawFootShade(f, f.px(118), lightFoot ? 250 : 235, lightFoot);
    const QRect content = f.content();
    const QRect titleRect(content.left(), content.bottom() - f.px(44) + 1, content.width(), f.px(44));
    const QFont font = fitFont(f, uiFont(f, 12, QFont::Black), headline, titleRect.size(), 14, 10, 1.14);
    const Laid measured = layoutText(nullptr, p->device(), QRectF(titleRect), headline, font, QColor(), 1.14);
    const qreal titleTop = titleRect.bottom() + 1 - measured.height;
    layoutText(p, p->device(), QRectF(titleRect.left(), titleTop, titleRect.width(), measured.height + 1), headline, font,
               wordInk(lightFoot, 250), 1.14);
    QFont kicker = uiFont(f, 7.5, QFont::Black);
    kicker.setLetterSpacing(QFont::PercentageSpacing, 118);
    CardRenderUtils::drawElidedText(p, QRect(content.left(), qRound(titleTop) - f.px(16), content.width(), f.px(14)),
                                    CardRenderUtils::displayHost(url).toUpper(), kicker,
                                    lightFoot ? QColor(QStringLiteral("#A3261C")) : QColor(QStringLiteral("#FFE08A")),
                                    Qt::AlignLeft | Qt::AlignVCenter);
}

// A bare site: its name is the poster. The name large, its ending in another colour, a disc of the opposite colour behind.
void paintDomain(const Frame &f, const QUrl &url, const CardData &card) {
    QPainter *p = f.painter;
    const QString host = CardRenderUtils::displayHost(url);
    const QColor site = CardRenderUtils::webSiteColor(url, card.favicon).toHsl();
    const qreal hue = qMax<qreal>(0.0, site.hslHueF());
    p->fillRect(f.panel, QColor::fromHslF(hue, qMin<qreal>(site.hslSaturationF(), 0.60), 0.26));
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    // A tomato disc; cobalt behind a site that is itself red or orange.
    const qreal degrees = hue * 360.0;
    p->setBrush(degrees < 50.0 || degrees > 330.0 ? QColor(QStringLiteral("#2E4BD8")) : QColor(QStringLiteral("#E4572E")));
    const qreal radius = f.panel.width() * 0.39;
    p->drawEllipse(QPointF(f.panel.left() + f.panel.width() * 0.27, f.panel.top() + f.panel.height() * 0.98), radius, radius);
    p->restore();

    // "example" and ".org"; "example" and ".co.uk".
    QStringList labels = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    QString ending;
    static const QSet<QString> second = {QStringLiteral("com"), QStringLiteral("co"), QStringLiteral("org"),
        QStringLiteral("net"), QStringLiteral("gov"), QStringLiteral("edu"), QStringLiteral("ac")};
    if (labels.size() >= 3 && labels.last().size() == 2 && second.contains(labels.at(labels.size() - 2))) {
        ending = QLatin1Char('.') + labels.at(labels.size() - 2) + QLatin1Char('.') + labels.last();
        labels.removeLast();
        labels.removeLast();
    } else if (labels.size() >= 2) {
        ending = QLatin1Char('.') + labels.takeLast();
    }
    const QString name = labels.join(QLatin1Char('.'));
    const QRect content = f.content();
    QFont font = uiFont(f, 12, QFont::Black);
    font.setStretch(90);
    const QRect nameRect(content.left(), content.top(), content.width(), content.height() * 62 / 100);
    font = fitFont(f, font, name, nameRect.size(), 32, 12, 1.0);
    const Laid laid = layoutText(p, p->device(), QRectF(nameRect), name, font, QColor(QStringLiteral("#F4EFE4")), 1.0);
    layoutText(p, p->device(), QRectF(content.left(), nameRect.top() + laid.height, content.width(), content.height() - laid.height),
               ending, font, QColor(QStringLiteral("#FFD479")), 1.0);
}

// A long headline: set as a newspaper's, a serif face on paper, the site's colour down the spine.
void paintHeadline(const Frame &f, const QUrl &url, const CardData &card, const QString &headline, const QString &context) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#F3ECDF")));
    const QColor site = CardRenderUtils::webSiteColor(url, card.favicon).toHsl();
    const QColor spine = QColor::fromHslF(qMax<qreal>(0.0, site.hslHueF()), qMin<qreal>(site.hslSaturationF(), 0.70), 0.40);
    p->fillRect(QRect(f.panel.left(), f.panel.top(), f.px(8), f.panel.height()), spine);
    const QRect content = f.content().adjusted(f.px(8), 0, 0, 0);
    const QString host = CardRenderUtils::displayHost(url);
    QFont kicker = uiFont(f, 7.5, QFont::Black);
    kicker.setLetterSpacing(QFont::PercentageSpacing, 118);
    const int kickerHeight = f.px(16);
    CardRenderUtils::drawElidedText(p, QRect(content.left(), content.top(), content.width(), kickerHeight),
                                    context.isEmpty() ? host.toUpper() : host.toUpper() + QStringLiteral(" \u00B7 ") + context.toUpper(),
                                    kicker, spine, Qt::AlignLeft | Qt::AlignVCenter);
    const QRect titleRect = content.adjusted(0, kickerHeight + f.px(3), 0, 0);
    QFont font = serifFont(f, 12, false);
    font.setWeight(QFont::Black);
    font = fitFont(f, font, headline, titleRect.size(), 14.5, 9.5, 1.2);
    layoutText(p, p->device(), QRectF(titleRect), headline, font, QColor(QStringLiteral("#1B1721")), 1.2);
}

void paintLink(const Frame &f, const QUrl &url, const CardData &card, int loadingPhase, bool withPicture) {
    if (withPicture) {
        paintLinkPicture(f, card, loadingPhase, url);
        return;
    }
    QString context;
    const QString headline = CardRenderUtils::webHeadline(url, card.title, &context);
    const QString host = CardRenderUtils::displayHost(url);
    const bool bare = (url.path().isEmpty() || url.path() == QLatin1String("/")) && !url.hasQuery() && headline == host;
    if (bare && !host.isEmpty()) {
        paintDomain(f, url, card);
    } else if (headline.size() > (mostlyLatin(headline) ? 34 : 17)) {
        paintHeadline(f, url, card, headline, context);
    } else {
        paintCover(f, url, card);
    }
}

// --- files ---

enum class FileKind { Other, Pdf, Document, Sheet, Slides, Archive, Media, Picture, Code, Folder };

FileKind fileKind(const QString &suffix, bool folder) {
    static const QHash<QString, FileKind> kinds = [] {
        QHash<QString, FileKind> map;
        auto add = [&map](FileKind kind, std::initializer_list<const char *> names) {
            for (const char *name : names) {
                map.insert(QString::fromLatin1(name), kind);
            }
        };
        add(FileKind::Pdf, {"pdf"});
        add(FileKind::Document, {"doc", "docx", "odt", "rtf", "txt", "md", "pages"});
        add(FileKind::Sheet, {"xls", "xlsx", "csv", "ods", "numbers"});
        add(FileKind::Slides, {"ppt", "pptx", "odp", "key"});
        add(FileKind::Archive, {"zip", "7z", "rar", "tar", "gz", "xz", "bz2", "zst", "deb", "rpm", "dmg", "iso"});
        add(FileKind::Media, {"mp3", "flac", "wav", "ogg", "m4a", "mp4", "mkv", "mov", "webm", "avi"});
        add(FileKind::Picture, {"png", "jpg", "jpeg", "gif", "webp", "svg", "bmp", "heic", "psd"});
        add(FileKind::Code, {"c", "h", "cpp", "hpp", "py", "js", "ts", "rs", "go", "java", "kt", "sh", "json", "yml",
                             "yaml", "toml", "xml", "html", "css", "qml", "sql"});
        return map;
    }();
    return folder ? FileKind::Folder : kinds.value(suffix, FileKind::Other);
}

QColor fileKindColor(FileKind kind) {
    switch (kind) {
        case FileKind::Pdf: return QColor(QStringLiteral("#E4392F"));
        case FileKind::Document: return QColor(QStringLiteral("#2F6FE4"));
        case FileKind::Sheet: return QColor(QStringLiteral("#1F9D57"));
        case FileKind::Slides: return QColor(QStringLiteral("#E8743A"));
        case FileKind::Archive: return QColor(QStringLiteral("#C4901A"));
        case FileKind::Media: return QColor(QStringLiteral("#8B5CF6"));
        case FileKind::Picture: return QColor(QStringLiteral("#DB4C8B"));
        case FileKind::Code: return QColor(QStringLiteral("#0E9F9A"));
        case FileKind::Folder: return QColor(QStringLiteral("#D99E2E"));
        case FileKind::Other: break;
    }
    return QColor(QStringLiteral("#4B5873"));
}

struct FileName {
    QString name;       // without its extension
    QString giant;      // "PDF", "FILE"
    FileKind kind = FileKind::Other;
};

FileName describeFile(const QUrl &url) {
    FileName file;
    const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : url.path());
    const bool folder = url.isLocalFile() && info.isDir();
    const QString suffix = folder ? QString() : info.suffix().toLower();
    file.name = folder || suffix.isEmpty() ? info.fileName() : info.completeBaseName();
    if (file.name.isEmpty()) {
        file.name = url.toDisplayString(QUrl::PreferLocalFile);
    }
    file.giant = suffix.isEmpty() || suffix.size() > 4 ? QStringLiteral("FILE") : suffix.toUpper();
    file.kind = fileKind(suffix, folder);
    return file;
}

// Each kind's own mark across the slab, behind its letters.
void drawKindPattern(const Frame &f, FileKind kind) {
    QPainter *p = f.painter;
    const QColor line(18, 15, 10, 70);
    const int top = f.panel.top() + f.panel.height() * 36 / 100;
    switch (kind) {
        case FileKind::Sheet:
            // Cells.
            for (int y = top; y < f.panel.bottom(); y += f.px(26)) {
                p->fillRect(QRect(f.panel.left(), y, f.panel.width(), f.px(2)), line);
            }
            for (int x = f.panel.left() + f.px(64); x < f.panel.right(); x += f.px(64)) {
                p->fillRect(QRect(x, top, f.px(2), f.panel.bottom() - top + 1), line);
            }
            break;
        case FileKind::Document:
            // Lines of writing.
            for (int y = top; y < f.panel.bottom(); y += f.px(12)) {
                p->fillRect(QRect(f.panel.left() + f.pad(), y, f.panel.width() - f.pad() * 2, f.px(2)), QColor(18, 15, 10, 46));
            }
            break;
        case FileKind::Archive: {
            // A zip's teeth down the right.
            const int x = f.panel.right() - f.px(30);
            p->fillRect(QRect(x, f.panel.top(), f.px(2), f.panel.height()), line);
            for (int y = f.panel.top() + f.px(4), i = 0; y < f.panel.bottom(); y += f.px(8), ++i) {
                p->fillRect(QRect(i % 2 ? x + f.px(2) : x - f.px(8), y, f.px(8), f.px(4)), QColor(18, 15, 10, 110));
            }
            break;
        }
        case FileKind::Media: {
            // A record's grooves from the corner.
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(line, f.px(2)));
            for (int r = f.px(40); r <= f.px(160); r += f.px(24)) {
                p->drawEllipse(QPointF(f.panel.right() - f.px(10), f.panel.top() + f.px(6)), r, r);
            }
            p->restore();
            break;
        }
        default:
            break;
    }
}

// A file: a slab of its kind's colour and mark, its extension in letters that run off the foot.
void paintSlab(const Frame &f, const FileName &file) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, fileKindColor(file.kind));
    drawKindPattern(f, file.kind);
    drawGiant(p, file.giant, QRectF(f.panel.left() - f.px(4), f.panel.top() + f.panel.height() * 0.46, f.panel.width() * 0.98,
                                    f.panel.height() * 0.78),
              QColor(18, 15, 10, 232));
    const QRect content = f.content();
    const QRect nameRect(content.left(), content.top(), content.width(), qRound(f.panel.height() * 0.36));
    const QFont nameFont = fitFont(f, uiFont(f, 12, QFont::Black), file.name, nameRect.size(), 16, 9.5, 1.12);
    layoutText(p, p->device(), QRectF(nameRect), file.name, nameFont, QColor(255, 255, 255, 250), 1.12);
}

// A folder: a manila folder, its name on the cover and where it is kept below.
void paintFolder(const Frame &f, const FileName &file, const QString &where) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#16202E")));
    const QRectF tab(f.panel.left() + f.px(22), f.panel.top() + f.px(18), f.px(96), f.px(30));
    const QRectF cover(f.panel.left() + f.px(22), f.panel.top() + f.px(38), f.panel.width() - f.px(44), f.panel.height());
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(QStringLiteral("#E9B44C")));
    p->drawRoundedRect(tab, f.px(9), f.px(9));
    QLinearGradient manila(cover.topLeft(), cover.bottomLeft());
    manila.setColorAt(0.0, QColor(QStringLiteral("#F2C462")));
    manila.setColorAt(0.6, QColor(QStringLiteral("#D99E2E")));
    p->setBrush(manila);
    p->drawRoundedRect(cover, f.px(8), f.px(8));
    p->restore();
    const QRect text(qRound(cover.left()) + f.px(14), qRound(cover.top()) + f.px(12), qRound(cover.width()) - f.px(28), f.px(52));
    const QFont nameFont = fitFont(f, uiFont(f, 12, QFont::Black), file.name, text.size(), 16.5, 10, 1.12);
    const Laid laid = layoutText(p, p->device(), QRectF(text), file.name, nameFont, QColor(18, 15, 10, 240), 1.12);
    CardRenderUtils::drawElidedText(p, QRect(text.left(), text.top() + qRound(laid.height) + f.px(2), text.width(), f.px(16)), where,
                                    monoFont(f, 8, QFont::Bold), QColor(QStringLiteral("#5A4310")), Qt::AlignLeft | Qt::AlignVCenter,
                                    Qt::ElideMiddle);
}

// Several files: how many, in letters off the foot, and the first few by name with their kind's tag.
void paintStack(const Frame &f, const QList<QUrl> &urls) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#4B5873")));
    drawGiant(p, QStringLiteral("\u00D7%1").arg(urls.size()),
              QRectF(f.panel.left() + f.panel.width() * 0.30, f.panel.top() + f.panel.height() * 0.40, f.panel.width() * 0.68,
                     f.panel.height() * 0.84),
              QColor(18, 15, 10, 232), true);
    const QRect content = f.content();
    const QFont chipFont = monoFont(f, 7.5, QFont::Bold);
    const QFont nameFont = uiFont(f, 10, QFont::Bold);
    const int row = f.px(23);
    for (int i = 0; i < qMin(3, int(urls.size())); ++i) {
        const FileName file = describeFile(urls.at(i));
        const int y = content.top() + i * row;
        const int chip = drawChip(f, content.left(), y + f.px(2), file.kind == FileKind::Folder ? QStringLiteral("DIR") : file.giant,
                                  fileKindColor(file.kind), Qt::white, chipFont);
        CardRenderUtils::drawElidedText(p, QRect(content.left() + chip + f.px(8), y, content.width() - chip - f.px(8), row), file.name,
                                        nameFont, QColor(255, 255, 255, 250), Qt::AlignLeft | Qt::AlignVCenter, Qt::ElideMiddle);
    }
}

// A picture file: the picture, its kind and its name on tags hung from the left edge.
void paintPictureFile(const Frame &f, const QPixmap &picture, const FileName &file, const QString &cacheName) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#0D0E12")));
    CardRenderUtils::drawCoverPixmap(p, f.panel, picture, cacheName, QSize());
    const QFont chipFont = monoFont(f, 8, QFont::Bold);
    const int chipHeight = QFontMetrics(chipFont, p->device()).height() + f.px(4);
    drawChip(f, f.panel.left(), f.panel.top() + f.px(12), file.giant, fileKindColor(FileKind::Picture), Qt::white, chipFont, true);
    const QFont nameFont = uiFont(f, 9.5, QFont::Bold);
    const QString name = QFontMetrics(nameFont, p->device()).elidedText(file.name, Qt::ElideMiddle, f.panel.width() * 2 / 3);
    drawChip(f, f.panel.left(), f.panel.top() + f.px(12) + chipHeight + f.px(4), name, QColor(18, 15, 10, 235), Qt::white, nameFont, true);
}

// --- colour ---

// A colour: a printer's chip. The colour above, its values on the white below.
void paintChip(const Frame &f, const QColor &color) {
    QPainter *p = f.painter;
    p->fillRect(f.panel, QColor(QStringLiteral("#F6F3EC")));
    const int block = f.panel.height() * 54 / 100;
    p->fillRect(QRect(f.panel.left(), f.panel.top(), f.panel.width(), block), QColor(color.red(), color.green(), color.blue()));
    const QRect text(f.panel.left() + f.pad(), f.panel.top() + block + f.px(6), f.panel.width() - f.pad() * 2, f.px(24));
    CardRenderUtils::drawElidedText(p, text, color.name(QColor::HexRgb).toUpper(), uiFont(f, 14.5, QFont::Black), QColor(18, 15, 10),
                                    Qt::AlignLeft | Qt::AlignVCenter);
    const QColor hsl = color.toHsl();
    const QString values = QStringLiteral("RGB %1 \u00B7 %2 \u00B7 %3   HSL %4\u00B0 %5% %6%")
        .arg(color.red()).arg(color.green()).arg(color.blue())
        .arg(qMax(0, hsl.hslHue())).arg(qRound(hsl.hslSaturationF() * 100)).arg(qRound(hsl.lightnessF() * 100));
    CardRenderUtils::drawElidedText(p, QRect(text.left(), text.bottom() + 1, text.width(), f.px(14)), values,
                                    monoFont(f, 7.5, QFont::Medium), QColor(QStringLiteral("#5D554D")), Qt::AlignLeft | Qt::AlignVCenter);
}

// Code: one command on the phosphor screen, several lines in the editor.
void paintCode(const Frame &f, const QString &text) {
    if (text.trimmed().contains(QLatin1Char('\n'))) {
        paintEditor(f, text.trimmed());
    } else {
        paintTerminal(f, text.trimmed());
    }
}

} // namespace

void PosterCard::paint(QPainter *painter, const QRect &panel, const CardData &card, const Words &words,
                       const QColor &appTone, int scale, qreal dpr, int loadingPhase,
                       const QModelIndex &index, const ClipboardCardDelegate *delegate) {
    const Frame f {painter, panel, scale};
    const QString text = CardRenderUtils::previewTextForCard(card);

    switch (card.contentType) {
        case Color: {
            const QColor color = card.color.isValid() ? card.color : ColorCardBody::colorFromText(text.trimmed());
            paintChip(f, color.isValid() ? color : QColor(QStringLiteral("#4A5F7A")));
            return;
        }
        case Image:
            paintImage(f, card, loadingPhase);
            return;
        case Office:
            paintOnGraphPaper(f, card, loadingPhase);
            return;
        case Link: {
            const QUrl url = !card.url.isEmpty() ? QUrl(card.url)
                : (!card.normalizedUrls.isEmpty() ? card.normalizedUrls.first() : QUrl(card.normalizedText.left(512).trimmed()));
            paintLink(f, url, card, loadingPhase, LinkCardBody::showsPagePicture(card));
            return;
        }
        case File: {
            if (card.normalizedUrls.size() != 1) {
                paintStack(f, card.normalizedUrls);
                return;
            }
            const QUrl url = card.normalizedUrls.first();
            const FileName file = describeFile(url);
            if (url.isLocalFile()) {
                // A picture file shows itself.
                const QPixmap thumb = delegate->localImageThumbnail(url.toLocalFile(), panel.size(), dpr, index);
                if (!thumb.isNull()) {
                    paintPictureFile(f, thumb, file, card.name);
                    return;
                }
            }
            if (file.kind == FileKind::Folder) {
                paintFolder(f, file, words.count);
            } else {
                paintSlab(f, file);
            }
            return;
        }
        case RichText: {
            const bool drawing = MxGraphRenderer::isMxGraphContent(card.normalizedText);
            if (drawing) {
                paintBlueprint(f, card, loadingPhase);
            } else if (card.normalizedText.trimmed().isEmpty()) {
                // Nothing to quote: its picture.
                paintPlate(f, card, loadingPhase);
            } else if (words.code) {
                paintCode(f, text);
            } else if (card.normalizedText.contains(QLatin1Char('\t')) && pictureReady(card)) {
                // A table: as it was drawn.
                paintTapedSheet(f, card, words.darkTheme);
            } else if (!paintArticle(f, card.normalizedText)) {
                paintQuote(f, text);
            }
            return;
        }
        case Text:
        case All:
            break;
    }

    const QString trimmed = card.normalizedText.trimmed();
    if (const QColor color = ColorCardBody::colorFromText(trimmed); color.isValid()) {
        paintChip(f, color);
    } else if (const QUrl url = CardRenderUtils::singleWebUrl(card.normalizedText); url.isValid()) {
        paintLink(f, url, card, loadingPhase, false);
    } else if (words.code) {
        paintCode(f, text);
    } else if (looksLikeFigure(trimmed)) {
        paintFigure(f, trimmed);
    } else if (looksLikePath(trimmed)) {
        paintPath(f, trimmed);
    } else if (looksLikeToken(text)) {
        paintTicket(f, text);
    } else if (const QStringList lines = shortLines(trimmed); !lines.isEmpty()) {
        paintList(f, lines);
    } else if (trimmed.size() > 40) {
        paintParagraph(f, trimmed, appTone);
    } else {
        paintWords(f, text, appTone);
    }
}
