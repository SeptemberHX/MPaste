// input: MathML XML strings produced by MathType / equation editors.
// output: QPixmap renderings using only Qt Widgets + Xml + GUI fonts.
// pos: data layer rendering helper. Pure function, no globals, no I/O.
// update: If I change, update my folder README.md.
#include "MathMLRenderer.h"

#include "CardPreviewMetrics.h"

#include <QDebug>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QXmlStreamReader>
#include <QtMath>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace {

constexpr qreal kBaseFontPx = 24.0;
constexpr qreal kScriptScale = 0.72;
constexpr qreal kScriptScaleMin = 0.55;
// Italic slant in tan(angle). Standard typographic italic is around 0.25
// (~14°). Cambria Math italic feels too steep next to upright operators
// here, so we render math italic via an oblique shear at a gentler angle
// instead of using the font's italic variant. Tweak this constant alone to
// retune the slant without touching layout code.
constexpr qreal kItalicSlant = 0.15;  // ~8.5°

enum class Kind {
    Token,        // mi, mn, mo, mtext, ms
    Row,          // mrow, math, semantics
    Sub,          // msub
    Sup,          // msup
    SubSup,       // msubsup
    Frac,         // mfrac
    Sqrt,         // msqrt, mroot (index ignored)
    Under,        // munder
    Over,         // mover
    UnderOver,    // munderover
    Space,        // mspace
    Fenced,       // mfenced (open + children w/ separators + close)
    Table,        // mtable
    TableRow,     // mtr
    TableCell,    // mtd
    Unknown,
};

enum class Variant {
    Default,
    Normal,
    Bold,
    Italic,
    BoldItalic,
    DoubleStruck,
    Script,
    BoldScript,
    Fraktur,
    BoldFraktur,
    SansSerif,
    BoldSansSerif,
    SansSerifItalic,
    SansSerifBoldItalic,
    Monospace,
};

struct Node {
    Kind kind = Kind::Unknown;
    QString text;             // token contents
    QString elementName;      // lowercased element name
    Variant variant = Variant::Default;
    qreal spaceWidth = 0;     // mspace width override
    QString fenceOpen;        // mfenced open attr
    QString fenceClose;       // mfenced close attr
    QString fenceSeparators;  // mfenced separators attr
    std::vector<std::unique_ptr<Node>> children;
};

struct Box {
    qreal width = 0;
    qreal ascent = 0;
    qreal descent = 0;
    std::function<void(QPainter &painter, qreal x, qreal baseline)> draw;
    // Non-zero when this Box represents a single bracket / brace / paren
    // operator that should grow vertically to match its containing row's
    // tallest sibling. The post-process pass in layoutRowOfChildren reads
    // this and replaces draw / dimensions accordingly.
    QChar stretchyHint;
    qreal height() const { return ascent + descent; }
    bool empty() const { return width <= 0 && ascent <= 0 && descent <= 0; }
};

struct Ctx {
    qreal fontSize;
};

Variant parseVariant(QStringView s) {
    const QString v = s.trimmed().toString().toLower();
    if (v == QLatin1String("normal")) return Variant::Normal;
    if (v == QLatin1String("bold")) return Variant::Bold;
    if (v == QLatin1String("italic")) return Variant::Italic;
    if (v == QLatin1String("bold-italic")) return Variant::BoldItalic;
    if (v == QLatin1String("double-struck")) return Variant::DoubleStruck;
    if (v == QLatin1String("script")) return Variant::Script;
    if (v == QLatin1String("bold-script")) return Variant::BoldScript;
    if (v == QLatin1String("fraktur")) return Variant::Fraktur;
    if (v == QLatin1String("bold-fraktur")) return Variant::BoldFraktur;
    if (v == QLatin1String("sans-serif")) return Variant::SansSerif;
    if (v == QLatin1String("bold-sans-serif")) return Variant::BoldSansSerif;
    if (v == QLatin1String("sans-serif-italic")) return Variant::SansSerifItalic;
    if (v == QLatin1String("sans-serif-bold-italic")) return Variant::SansSerifBoldItalic;
    if (v == QLatin1String("monospace")) return Variant::Monospace;
    return Variant::Default;
}

// Map a single ASCII letter or digit to its Unicode Mathematical Alphanumeric
// codepoint for the given variant. Falls back to the original code if no
// mapping exists. Greek letters are not currently mapped (would need a
// separate table) — they render with the base font's italic style.
uint mapMathAlphanumeric(uint code, Variant v) {
    auto inRange = [&](uint lo, uint hi) { return code >= lo && code <= hi; };
    const bool upper = inRange('A', 'Z');
    const bool lower = inRange('a', 'z');
    const bool digit = inRange('0', '9');
    if (!upper && !lower && !digit) {
        return code;
    }
    auto mapLetter = [&](uint baseUpperA, uint baseLowerA) -> uint {
        if (upper) return baseUpperA + (code - 'A');
        if (lower) return baseLowerA + (code - 'a');
        return code;
    };
    auto mapDigit = [&](uint base) -> uint {
        return base + (code - '0');
    };
    switch (v) {
        case Variant::Bold:
            if (digit) return mapDigit(0x1D7CE);
            return mapLetter(0x1D400, 0x1D41A);
        case Variant::Italic:
            // U+1D455 (italic h) is reserved; the standard substitutes
            // U+210E (PLANCK CONSTANT). Handle it specially.
            if (lower && code == 'h') return 0x210E;
            return mapLetter(0x1D434, 0x1D44E);
        case Variant::BoldItalic:
            return mapLetter(0x1D468, 0x1D482);
        case Variant::Script:
            // Several script letters are reserved and substituted from the
            // Letterlike Symbols block.
            if (upper) {
                switch (code) {
                    case 'B': return 0x212C;
                    case 'E': return 0x2130;
                    case 'F': return 0x2131;
                    case 'H': return 0x210B;
                    case 'I': return 0x2110;
                    case 'L': return 0x2112;
                    case 'M': return 0x2133;
                    case 'R': return 0x211B;
                    default: return 0x1D49C + (code - 'A');
                }
            } else if (lower) {
                switch (code) {
                    case 'e': return 0x212F;
                    case 'g': return 0x210A;
                    case 'o': return 0x2134;
                    default: return 0x1D4B6 + (code - 'a');
                }
            }
            return code;
        case Variant::BoldScript:
            return mapLetter(0x1D4D0, 0x1D4EA);
        case Variant::Fraktur:
            if (upper) {
                switch (code) {
                    case 'C': return 0x212D;
                    case 'H': return 0x210C;
                    case 'I': return 0x2111;
                    case 'R': return 0x211C;
                    case 'Z': return 0x2128;
                    default: return 0x1D504 + (code - 'A');
                }
            }
            return mapLetter(0x1D504, 0x1D51E);
        case Variant::BoldFraktur:
            return mapLetter(0x1D56C, 0x1D586);
        case Variant::DoubleStruck:
            if (upper) {
                switch (code) {
                    case 'C': return 0x2102;
                    case 'H': return 0x210D;
                    case 'N': return 0x2115;
                    case 'P': return 0x2119;
                    case 'Q': return 0x211A;
                    case 'R': return 0x211D;
                    case 'Z': return 0x2124;
                    default: return 0x1D538 + (code - 'A');
                }
            } else if (lower) {
                return 0x1D552 + (code - 'a');
            }
            if (digit) return mapDigit(0x1D7D8);
            return code;
        case Variant::SansSerif:
            if (digit) return mapDigit(0x1D7E2);
            return mapLetter(0x1D5A0, 0x1D5BA);
        case Variant::BoldSansSerif:
            if (digit) return mapDigit(0x1D7EC);
            return mapLetter(0x1D5D4, 0x1D5EE);
        case Variant::SansSerifItalic:
            return mapLetter(0x1D608, 0x1D622);
        case Variant::SansSerifBoldItalic:
            return mapLetter(0x1D63C, 0x1D656);
        case Variant::Monospace:
            if (digit) return mapDigit(0x1D7F6);
            return mapLetter(0x1D670, 0x1D68A);
        case Variant::Normal:
        case Variant::Default:
        default:
            return code;
    }
}

QString applyVariantMapping(const QString &text, Variant v) {
    if (v == Variant::Default || v == Variant::Normal) {
        return text;
    }
    QString out;
    out.reserve(text.size() * 2);
    for (int i = 0; i < text.size(); ) {
        uint code = text.at(i).unicode();
        int consumed = 1;
        if (text.at(i).isHighSurrogate() && i + 1 < text.size()
            && text.at(i + 1).isLowSurrogate()) {
            code = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            consumed = 2;
        }
        const uint mapped = mapMathAlphanumeric(code, v);
        if (mapped <= 0xFFFF) {
            out.append(QChar(static_cast<ushort>(mapped)));
        } else {
            out.append(QChar::highSurrogate(mapped));
            out.append(QChar::lowSurrogate(mapped));
        }
        i += consumed;
    }
    return out;
}

QFont mathFont(qreal sizePx, bool italic) {
    QFont f;
    // Prefer fonts with good math/Greek coverage. Fall back through Cambria,
    // Times, and DejaVu so renders look reasonable on stripped-down systems.
    f.setFamilies({
        QStringLiteral("Cambria Math"),
        QStringLiteral("STIX Two Math"),
        QStringLiteral("Latin Modern Math"),
        QStringLiteral("Cambria"),
        QStringLiteral("Times New Roman"),
        QStringLiteral("DejaVu Serif"),
    });
    f.setPixelSize(qMax(8, qRound(sizePx)));
    f.setItalic(italic);
    return f;
}

Box layoutNode(const Node &node, const Ctx &ctx);

Box layoutToken(const Node &node, const Ctx &ctx) {
    if (node.text.isEmpty()) {
        return {};
    }
    bool italic = false;
    bool bold = false;
    bool sansSerif = false;
    bool monospace = false;
    qreal padLeft = 0;
    qreal padRight = 0;

    // Resolve mathvariant — either an explicit attribute or the MathML
    // default. The default for `mi` is: italic when the content is a SINGLE
    // Latin or Greek letter (variable name); upright otherwise. Multi-letter
    // mi (sin, log), CJK ideographs, digits, and punctuation must NOT be
    // italicized — slanted CJK looks broken, and italic digits look wrong.
    auto isMathLetter = [](QChar c) -> bool {
        const ushort u = c.unicode();
        // ASCII Latin letters
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')) return true;
        // Latin-1 supplement / Latin Extended (à é ñ ä etc.)
        if (u >= 0x00C0 && u <= 0x024F) return true;
        // Greek and Coptic (α β γ ... Ω, including η at U+03B7)
        if (u >= 0x0370 && u <= 0x03FF) return true;
        // Greek Extended
        if (u >= 0x1F00 && u <= 0x1FFF) return true;
        return false;
    };
    Variant effectiveVariant = node.variant;
    if (effectiveVariant == Variant::Default && node.elementName == QLatin1String("mi")) {
        const bool singleMathLetter = (node.text.size() == 1) && isMathLetter(node.text.at(0));
        effectiveVariant = singleMathLetter ? Variant::Italic : Variant::Normal;
    }
    switch (effectiveVariant) {
        case Variant::Italic:                italic = true; break;
        case Variant::Bold:                  bold = true; break;
        case Variant::BoldItalic:            bold = true; italic = true; break;
        case Variant::SansSerif:             sansSerif = true; break;
        case Variant::BoldSansSerif:         sansSerif = true; bold = true; break;
        case Variant::SansSerifItalic:       sansSerif = true; italic = true; break;
        case Variant::SansSerifBoldItalic:   sansSerif = true; bold = true; italic = true; break;
        case Variant::Monospace:             monospace = true; break;
        // Script / Fraktur / DoubleStruck render via Unicode codepoint
        // substitution; the font itself stays upright serif.
        default: break;
    }

    QString glyphText = applyVariantMapping(node.text, effectiveVariant);

    if (node.elementName == QLatin1String("mo")) {
        const QChar c = glyphText.size() == 1 ? glyphText.at(0) : QChar();
        const bool isFence = (c == QLatin1Char('(') || c == QLatin1Char(')')
                              || c == QLatin1Char('[') || c == QLatin1Char(']')
                              || c == QLatin1Char('{') || c == QLatin1Char('}')
                              || c == QChar(0x2329) || c == QChar(0x232A));
        const bool isPunct = (c == QLatin1Char(',') || c == QLatin1Char(';'));
        if (isFence) {
            // no padding
        } else if (isPunct) {
            padRight = ctx.fontSize * 0.18;
        } else {
            padLeft = ctx.fontSize * 0.18;
            padRight = ctx.fontSize * 0.18;
        }
    }
    QFont f;
    if (monospace) {
        f.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                       QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Courier New")});
    } else if (sansSerif) {
        f.setFamilies({QStringLiteral("Cambria Math"), QStringLiteral("Segoe UI"),
                       QStringLiteral("DejaVu Sans"), QStringLiteral("Arial")});
    } else {
        // Use the upright math font even for italic — slant is applied via
        // a QPainter shear in the draw closure so we can control the angle.
        f = mathFont(ctx.fontSize, /*italic=*/false);
    }
    f.setPixelSize(qMax(8, qRound(ctx.fontSize)));
    f.setItalic(false);
    f.setBold(bold);
    QFontMetricsF fm(f);
    const qreal w = fm.horizontalAdvance(glyphText);
    Box b;
    b.width = padLeft + w + padRight;
    b.ascent = fm.ascent();
    b.descent = fm.descent();
    const bool applyShear = italic;
    b.draw = [f, glyphText, padLeft, applyShear](QPainter &p, qreal x, qreal baseline) {
        p.save();
        p.setFont(f);
        if (applyShear) {
            // Translate origin to the glyph's baseline, then shear the
            // x axis based on y so the top of the glyph slides right by
            // (kItalicSlant * height) while the baseline stays put.
            p.translate(x + padLeft, baseline);
            p.shear(-kItalicSlant, 0);
            p.drawText(QPointF(0, 0), glyphText);
        } else {
            p.drawText(QPointF(x + padLeft, baseline), glyphText);
        }
        p.restore();
    };
    // Mark single-character bracket / brace / paren / bar operators so the
    // enclosing mrow can stretch them to match content height.
    if (node.elementName == QLatin1String("mo") && glyphText.size() == 1) {
        const QChar c = glyphText.at(0);
        const ushort u = c.unicode();
        const bool isStretchyChar =
               u == '(' || u == ')'
            || u == '[' || u == ']'
            || u == '{' || u == '}'
            || u == '|'                    // single vertical bar (abs value)
            || u == 0x2016                 // ‖ double vertical line (norm)
            || u == 0x27E8 || u == 0x27E9  // ⟨ ⟩ angle brackets
            || u == 0x2308 || u == 0x2309  // ⌈ ⌉ ceiling
            || u == 0x230A || u == 0x230B; // ⌊ ⌋ floor
        if (isStretchyChar) {
            b.stretchyHint = c;
        }
    }
    return b;
}

// Build a Box that draws a vertically-stretched bracket / brace / paren by
// rendering the actual font glyph at an enlarged pixel size. Cambria Math
// (and most math-friendly serif fonts) have well-shaped {, }, (, ), [, ]
// glyphs that scale proportionally — using them gives a much better look
// than describing the shape with QPainterPath. The font's natural metrics
// place the brace centered around the math axis, which is exactly what we
// want next to a centered mtable.
Box buildStretchedBracket(QChar bracket, qreal contentAsc, qreal contentDesc, const Ctx &ctx) {
    const qreal axis = ctx.fontSize * 0.30;
    const qreal contentH = contentAsc + contentDesc;
    if (contentH <= 0) {
        return {};
    }

    // Pick a font size that makes the glyph cover the requested height.
    // Cambria Math's bracket glyphs roughly match the pixel size, with a
    // small slack — bump by 1.05 so visual height matches content.
    const qreal targetPixel = std::ceil(contentH * 1.05);
    QFont f;
    f.setFamilies({
        QStringLiteral("Cambria Math"),
        QStringLiteral("STIX Two Math"),
        QStringLiteral("Latin Modern Math"),
        QStringLiteral("Cambria"),
        QStringLiteral("Times New Roman"),
        QStringLiteral("DejaVu Serif"),
    });
    f.setPixelSize(qMax(8, qRound(targetPixel)));

    QFontMetricsF fm(f);
    const QString glyph(bracket);
    const qreal glyphW = fm.horizontalAdvance(glyph);
    const qreal glyphAsc = fm.ascent();
    const qreal glyphDesc = fm.descent();
    const qreal glyphH = glyphAsc + glyphDesc;
    if (glyphW <= 0 || glyphH <= 0) {
        return {};
    }

    Box b;
    // A small horizontal margin so adjacent content doesn't touch the brace.
    const qreal sideGap = ctx.fontSize * 0.08;
    b.width = glyphW + sideGap * 2;
    // Center the glyph vertically around the math axis (slightly above the
    // host row's baseline), matching the convention used by mtable.
    b.ascent = glyphH / 2.0 + axis;
    b.descent = glyphH / 2.0 - axis;

    b.draw = [f, glyph, glyphAsc, glyphDesc, sideGap, axis](QPainter &p, qreal x, qreal baseline) {
        // The glyph's vertical center should sit on the math axis line
        // (baseline - axis). Translate the font's baseline accordingly.
        const qreal glyphCenter = baseline - axis;
        const qreal drawBaseline = glyphCenter + (glyphAsc - glyphDesc) / 2.0;
        p.save();
        p.setFont(f);
        p.drawText(QPointF(x + sideGap, drawBaseline), glyph);
        p.restore();
    };
    b.stretchyHint = bracket;
    return b;
}

Box layoutRowOfChildren(const std::vector<std::unique_ptr<Node>> &children, const Ctx &ctx) {
    auto kids = std::make_shared<std::vector<Box>>();
    kids->reserve(children.size());
    for (const auto &child : children) {
        Box b = layoutNode(*child, ctx);
        if (b.empty()) {
            continue;
        }
        kids->push_back(std::move(b));
    }

    // Stretchy bracket post-process: if any child is a single bracket / brace
    // / paren operator AND there's a notably tall sibling (mtable, big mfrac,
    // etc.) in the same row, replace the bracket's draw with a path-drawn
    // version sized to the tall sibling. This is what makes the
    // "= { mtable" cases construct render correctly.
    qreal contentAsc = 0;
    qreal contentDesc = 0;
    for (const Box &b : *kids) {
        if (!b.stretchyHint.isNull()) continue;
        contentAsc = std::max(contentAsc, b.ascent);
        contentDesc = std::max(contentDesc, b.descent);
    }
    const qreal stretchTrigger = ctx.fontSize * 1.6;
    bool hasStretchy = false;
    for (const Box &b : *kids) {
        if (!b.stretchyHint.isNull()) { hasStretchy = true; break; }
    }
    if (hasStretchy) {
        qInfo().noquote() << QStringLiteral("[mathml] row stretchy check kids=%1 contentAsc=%2 contentDesc=%3 trigger=%4 fired=%5")
            .arg(kids->size())
            .arg(contentAsc, 0, 'f', 1)
            .arg(contentDesc, 0, 'f', 1)
            .arg(stretchTrigger, 0, 'f', 1)
            .arg(contentAsc + contentDesc >= stretchTrigger ? 1 : 0);
    }
    if (contentAsc + contentDesc >= stretchTrigger) {
        for (Box &b : *kids) {
            if (b.stretchyHint.isNull()) continue;
            // Skip if the bracket is already at least as tall as the content.
            if (b.ascent + b.descent >= contentAsc + contentDesc - 0.5) continue;
            qInfo().noquote() << QStringLiteral("[mathml] stretching '%1' to asc=%2 desc=%3")
                .arg(b.stretchyHint)
                .arg(contentAsc, 0, 'f', 1)
                .arg(contentDesc, 0, 'f', 1);
            b = buildStretchedBracket(b.stretchyHint, contentAsc, contentDesc, ctx);
        }
    }

    qreal totalW = 0;
    qreal asc = 0;
    qreal desc = 0;
    for (const Box &b : *kids) {
        totalW += b.width;
        asc = std::max(asc, b.ascent);
        desc = std::max(desc, b.descent);
    }

    Box out;
    out.width = totalW;
    out.ascent = asc;
    out.descent = desc;
    out.draw = [kids](QPainter &p, qreal x, qreal baseline) {
        qreal cx = x;
        for (const Box &k : *kids) {
            if (k.draw) k.draw(p, cx, baseline);
            cx += k.width;
        }
    };
    return out;
}

Box layoutSubSup(const Node &node, const Ctx &ctx, bool hasSub, bool hasSup) {
    if (node.children.empty()) {
        return {};
    }
    Box base = layoutNode(*node.children[0], ctx);
    if (base.empty()) {
        return {};
    }
    Ctx scriptCtx{std::max(ctx.fontSize * kScriptScale, ctx.fontSize * kScriptScaleMin)};
    Box sub;
    Box sup;
    int idx = 1;
    if (hasSub && idx < static_cast<int>(node.children.size())) {
        sub = layoutNode(*node.children[idx], scriptCtx);
        ++idx;
    }
    if (hasSup && idx < static_cast<int>(node.children.size())) {
        sup = layoutNode(*node.children[idx], scriptCtx);
        ++idx;
    }
    const qreal subShift = base.descent * 0.4 + sub.ascent * 0.35 + ctx.fontSize * 0.10;
    const qreal supShift = base.ascent * 0.50 + sup.descent * 0.30 + ctx.fontSize * 0.05;
    Box out;
    const qreal scriptW = std::max(sub.width, sup.width);
    // Trailing gap after the script. Italic subscript glyphs (e.g. i, j, f)
    // visibly extend past their advance width because of the italic slant,
    // so a following operator like ',' would otherwise touch them.
    const qreal scriptTrail = ctx.fontSize * 0.12;
    out.width = base.width + scriptW + scriptTrail;
    out.ascent = std::max(base.ascent, hasSup ? supShift + sup.ascent : 0.0);
    out.descent = std::max(base.descent, hasSub ? subShift + sub.descent : 0.0);
    auto baseShared = std::make_shared<Box>(std::move(base));
    auto subShared = std::make_shared<Box>(std::move(sub));
    auto supShared = std::make_shared<Box>(std::move(sup));
    const bool drawSub = hasSub && !subShared->empty();
    const bool drawSup = hasSup && !supShared->empty();
    out.draw = [baseShared, subShared, supShared, drawSub, drawSup, subShift, supShift]
        (QPainter &p, qreal x, qreal baseline) {
        baseShared->draw(p, x, baseline);
        const qreal scriptX = x + baseShared->width;
        if (drawSup) {
            supShared->draw(p, scriptX, baseline - supShift);
        }
        if (drawSub) {
            subShared->draw(p, scriptX, baseline + subShift);
        }
    };
    return out;
}

Box layoutFrac(const Node &node, const Ctx &ctx) {
    if (node.children.size() < 2) {
        return {};
    }
    Box num = layoutNode(*node.children[0], ctx);
    Box den = layoutNode(*node.children[1], ctx);
    if (num.empty() && den.empty()) {
        return {};
    }
    const qreal contentW = std::max(num.width, den.width);
    const qreal pad = ctx.fontSize * 0.20;
    const qreal w = contentW + pad * 2;
    const qreal axis = ctx.fontSize * 0.30;
    const qreal gap = ctx.fontSize * 0.14;
    const qreal numShift = num.descent + gap + axis;   // baseline up by this much
    const qreal denShift = den.ascent + gap - axis;    // baseline down
    Box out;
    out.width = w;
    out.ascent = numShift + num.ascent;
    out.descent = denShift + den.descent;
    auto numShared = std::make_shared<Box>(std::move(num));
    auto denShared = std::make_shared<Box>(std::move(den));
    const qreal lineThickness = std::max<qreal>(1.0, ctx.fontSize * 0.06);
    out.draw = [numShared, denShared, w, numShift, denShift, axis, lineThickness, pad]
        (QPainter &p, qreal x, qreal baseline) {
        const qreal numX = x + (w - numShared->width) / 2.0;
        const qreal denX = x + (w - denShared->width) / 2.0;
        numShared->draw(p, numX, baseline - numShift);
        denShared->draw(p, denX, baseline + denShift);
        const qreal lineY = baseline - axis;
        p.save();
        QPen pen(p.pen().color());
        pen.setWidthF(lineThickness);
        pen.setCapStyle(Qt::FlatCap);
        p.setPen(pen);
        p.drawLine(QPointF(x + pad * 0.4, lineY), QPointF(x + w - pad * 0.4, lineY));
        p.restore();
    };
    return out;
}

Box layoutSqrt(const Node &node, const Ctx &ctx) {
    if (node.children.empty()) {
        return {};
    }
    // msqrt has free children (treat as a row); mroot has [base, index] —
    // we ignore the index and render only the base.
    Box inner;
    if (node.elementName == QLatin1String("mroot") && node.children.size() >= 1) {
        inner = layoutNode(*node.children[0], ctx);
    } else {
        inner = layoutRowOfChildren(node.children, ctx);
    }
    if (inner.empty()) {
        return {};
    }
    const qreal hookW = ctx.fontSize * 0.55;
    const qreal padTop = ctx.fontSize * 0.12;
    const qreal padRight = ctx.fontSize * 0.10;
    Box out;
    out.width = hookW + inner.width + padRight;
    out.ascent = inner.ascent + padTop;
    out.descent = inner.descent;
    auto innerShared = std::make_shared<Box>(std::move(inner));
    const qreal lineThickness = std::max<qreal>(1.0, ctx.fontSize * 0.06);
    out.draw = [innerShared, hookW, padRight, lineThickness, padTop]
        (QPainter &p, qreal x, qreal baseline) {
        innerShared->draw(p, x + hookW, baseline);
        const qreal top = baseline - innerShared->ascent - padTop * 0.6;
        const qreal mid = baseline + innerShared->descent * 0.4;
        const qreal bot = baseline + innerShared->descent;
        p.save();
        QPen pen(p.pen().color());
        pen.setWidthF(lineThickness);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        QPainterPath path;
        path.moveTo(x, mid);
        path.lineTo(x + hookW * 0.30, bot);
        path.lineTo(x + hookW * 0.65, top);
        path.lineTo(x + hookW + innerShared->width + padRight, top);
        p.drawPath(path);
        p.restore();
    };
    return out;
}

Box layoutUnderOver(const Node &node, const Ctx &ctx, bool hasUnder, bool hasOver) {
    if (node.children.empty()) {
        return {};
    }
    Box base = layoutNode(*node.children[0], ctx);
    if (base.empty()) {
        return {};
    }
    Ctx scriptCtx{std::max(ctx.fontSize * kScriptScale, ctx.fontSize * kScriptScaleMin)};
    Box under;
    Box over;
    int idx = 1;
    if (hasUnder && idx < static_cast<int>(node.children.size())) {
        under = layoutNode(*node.children[idx], scriptCtx);
        ++idx;
    }
    if (hasOver && idx < static_cast<int>(node.children.size())) {
        over = layoutNode(*node.children[idx], scriptCtx);
        ++idx;
    }
    const qreal gap = ctx.fontSize * 0.08;
    Box out;
    out.width = std::max({base.width, under.width, over.width});
    out.ascent = base.ascent + (hasOver && !over.empty() ? over.height() + gap : 0.0);
    out.descent = base.descent + (hasUnder && !under.empty() ? under.height() + gap : 0.0);
    auto baseShared = std::make_shared<Box>(std::move(base));
    auto underShared = std::make_shared<Box>(std::move(under));
    auto overShared = std::make_shared<Box>(std::move(over));
    const qreal totalW = out.width;
    const bool drawUnder = hasUnder && !underShared->empty();
    const bool drawOver = hasOver && !overShared->empty();
    out.draw = [baseShared, underShared, overShared, drawUnder, drawOver, totalW, gap]
        (QPainter &p, qreal x, qreal baseline) {
        const qreal baseX = x + (totalW - baseShared->width) / 2.0;
        baseShared->draw(p, baseX, baseline);
        if (drawOver) {
            const qreal overX = x + (totalW - overShared->width) / 2.0;
            overShared->draw(p, overX, baseline - baseShared->ascent - gap - overShared->descent);
        }
        if (drawUnder) {
            const qreal underX = x + (totalW - underShared->width) / 2.0;
            underShared->draw(p, underX, baseline + baseShared->descent + gap + underShared->ascent);
        }
    };
    return out;
}

Box layoutFencedToken(const QString &text, const Ctx &ctx) {
    if (text.isEmpty()) {
        return {};
    }
    QFont f = mathFont(ctx.fontSize, false);
    QFontMetricsF fm(f);
    Box b;
    b.width = fm.horizontalAdvance(text);
    b.ascent = fm.ascent();
    b.descent = fm.descent();
    b.draw = [f, text](QPainter &p, qreal x, qreal baseline) {
        p.save();
        p.setFont(f);
        p.drawText(QPointF(x, baseline), text);
        p.restore();
    };
    return b;
}

Box layoutFenced(const Node &node, const Ctx &ctx) {
    // Default per MathML spec: open="(" close=")" separators=","
    const QString open = node.fenceOpen.isEmpty() ? QStringLiteral("(") : node.fenceOpen;
    const QString close = node.fenceClose.isEmpty() ? QStringLiteral(")") : node.fenceClose;
    const QString separators = node.fenceSeparators.isEmpty() ? QStringLiteral(",") : node.fenceSeparators;

    // First pass: lay out the inner children to learn their max height,
    // then decide whether to use stretched open/close fences. Separators
    // get rendered in the second pass once we know if we're going tall.
    std::vector<Box> innerBoxes;
    innerBoxes.reserve(node.children.size());
    qreal innerAsc = 0;
    qreal innerDesc = 0;
    for (const auto &child : node.children) {
        Box b = layoutNode(*child, ctx);
        if (b.empty()) continue;
        innerAsc = std::max(innerAsc, b.ascent);
        innerDesc = std::max(innerDesc, b.descent);
        innerBoxes.push_back(std::move(b));
    }

    const qreal stretchTrigger = ctx.fontSize * 1.6;
    const bool stretch = (innerAsc + innerDesc) >= stretchTrigger;

    auto buildFenceBox = [&](const QString &text) -> Box {
        if (text.isEmpty()) return {};
        if (stretch && text.size() == 1) {
            return buildStretchedBracket(text.at(0), innerAsc, innerDesc, ctx);
        }
        return layoutFencedToken(text, ctx);
    };

    auto kids = std::make_shared<std::vector<Box>>();
    qreal totalW = 0;
    qreal asc = 0;
    qreal desc = 0;

    auto pushBox = [&](Box b) {
        if (b.empty()) return;
        totalW += b.width;
        asc = std::max(asc, b.ascent);
        desc = std::max(desc, b.descent);
        kids->push_back(std::move(b));
    };

    pushBox(buildFenceBox(open));
    for (size_t i = 0; i < innerBoxes.size(); ++i) {
        if (i > 0) {
            const int sepIdx = std::min<int>(static_cast<int>(i) - 1, separators.size() - 1);
            if (sepIdx >= 0 && sepIdx < separators.size()) {
                Box sepBox = layoutFencedToken(QString(separators.at(sepIdx)), ctx);
                sepBox.width += ctx.fontSize * 0.18;
                pushBox(std::move(sepBox));
            }
        }
        pushBox(std::move(innerBoxes[i]));
    }
    pushBox(buildFenceBox(close));

    Box out;
    out.width = totalW;
    out.ascent = asc;
    out.descent = desc;
    out.draw = [kids](QPainter &p, qreal x, qreal baseline) {
        qreal cx = x;
        for (const Box &k : *kids) {
            if (k.draw) k.draw(p, cx, baseline);
            cx += k.width;
        }
    };
    return out;
}

Box layoutTable(const Node &node, const Ctx &ctx) {
    // Collect cell boxes row by row, skipping non-mtr children.
    std::vector<std::vector<Box>> grid;
    for (const auto &row : node.children) {
        if (row->kind != Kind::TableRow) {
            continue;
        }
        std::vector<Box> rowBoxes;
        for (const auto &cell : row->children) {
            if (cell->kind != Kind::TableCell) {
                continue;
            }
            // Cell content is laid out as a row.
            Box cellBox = layoutRowOfChildren(cell->children, ctx);
            rowBoxes.push_back(std::move(cellBox));
        }
        if (!rowBoxes.empty()) {
            grid.push_back(std::move(rowBoxes));
        }
    }
    if (grid.empty()) {
        return {};
    }

    size_t colCount = 0;
    for (const auto &r : grid) {
        colCount = std::max(colCount, r.size());
    }
    if (colCount == 0) {
        return {};
    }

    std::vector<qreal> colWidths(colCount, 0);
    std::vector<qreal> rowAscents(grid.size(), 0);
    std::vector<qreal> rowDescents(grid.size(), 0);
    for (size_t i = 0; i < grid.size(); ++i) {
        for (size_t j = 0; j < grid[i].size(); ++j) {
            const Box &c = grid[i][j];
            colWidths[j] = std::max(colWidths[j], c.width);
            rowAscents[i] = std::max(rowAscents[i], c.ascent);
            rowDescents[i] = std::max(rowDescents[i], c.descent);
        }
    }

    const qreal colGap = ctx.fontSize * 0.6;
    const qreal rowGap = ctx.fontSize * 0.35;

    qreal totalW = 0;
    for (qreal w : colWidths) {
        totalW += w;
    }
    if (colCount > 1) {
        totalW += (colCount - 1) * colGap;
    }

    qreal totalH = 0;
    for (size_t i = 0; i < grid.size(); ++i) {
        totalH += rowAscents[i] + rowDescents[i];
    }
    if (grid.size() > 1) {
        totalH += (grid.size() - 1) * rowGap;
    }

    // Center the table vertically around the math axis.
    const qreal axis = ctx.fontSize * 0.30;
    Box out;
    out.width = totalW;
    out.ascent = totalH / 2.0 + axis;
    out.descent = totalH / 2.0 - axis;

    auto gridShared = std::make_shared<std::vector<std::vector<Box>>>(std::move(grid));
    auto colWidthsShared = std::make_shared<std::vector<qreal>>(std::move(colWidths));
    auto rowAscentsShared = std::make_shared<std::vector<qreal>>(std::move(rowAscents));
    auto rowDescentsShared = std::make_shared<std::vector<qreal>>(std::move(rowDescents));

    out.draw = [gridShared, colWidthsShared, rowAscentsShared, rowDescentsShared,
                totalH, axis, colGap, rowGap]
        (QPainter &p, qreal x, qreal baseline) {
        const qreal startY = baseline - totalH / 2.0 - axis;
        qreal cy = startY;
        for (size_t i = 0; i < gridShared->size(); ++i) {
            const auto &row = (*gridShared)[i];
            const qreal rowAsc = (*rowAscentsShared)[i];
            const qreal rowDesc = (*rowDescentsShared)[i];
            const qreal cellBaseline = cy + rowAsc;
            qreal cx = x;
            for (size_t j = 0; j < row.size(); ++j) {
                const Box &cell = row[j];
                const qreal colW = (*colWidthsShared)[j];
                // Default cell alignment: center horizontally within column.
                const qreal cellX = cx + (colW - cell.width) / 2.0;
                if (cell.draw) {
                    cell.draw(p, cellX, cellBaseline);
                }
                cx += colW + colGap;
            }
            cy += rowAsc + rowDesc + rowGap;
        }
    };
    return out;
}

Box layoutNode(const Node &node, const Ctx &ctx) {
    switch (node.kind) {
        case Kind::Token:
            return layoutToken(node, ctx);
        case Kind::Row:
            return layoutRowOfChildren(node.children, ctx);
        case Kind::Sub:
            return layoutSubSup(node, ctx, true, false);
        case Kind::Sup:
            return layoutSubSup(node, ctx, false, true);
        case Kind::SubSup:
            return layoutSubSup(node, ctx, true, true);
        case Kind::Frac:
            return layoutFrac(node, ctx);
        case Kind::Sqrt:
            return layoutSqrt(node, ctx);
        case Kind::Under:
            return layoutUnderOver(node, ctx, true, false);
        case Kind::Over:
            return layoutUnderOver(node, ctx, false, true);
        case Kind::UnderOver:
            return layoutUnderOver(node, ctx, true, true);
        case Kind::Fenced:
            return layoutFenced(node, ctx);
        case Kind::Table:
            return layoutTable(node, ctx);
        case Kind::TableRow:
        case Kind::TableCell:
            // Should be reached only via layoutTable; treat as row for safety.
            return layoutRowOfChildren(node.children, ctx);
        case Kind::Space: {
            Box b;
            b.width = node.spaceWidth > 0 ? node.spaceWidth : ctx.fontSize * 0.25;
            b.ascent = ctx.fontSize * 0.6;
            b.descent = 0;
            b.draw = [](QPainter &, qreal, qreal) {};
            return b;
        }
        case Kind::Unknown:
        default:
            // Best effort: lay out children as a row.
            return layoutRowOfChildren(node.children, ctx);
    }
}

std::unique_ptr<Node> parseElement(QXmlStreamReader &xml);

std::unique_ptr<Node> finishElement(QXmlStreamReader &xml, std::unique_ptr<Node> node) {
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            auto child = parseElement(xml);
            if (child) {
                node->children.push_back(std::move(child));
            }
        } else if (xml.isEndElement()) {
            return node;
        } else if (xml.isCharacters()) {
            if (node->kind == Kind::Token) {
                node->text += xml.text().toString();
            }
        }
    }
    return node;
}

std::unique_ptr<Node> parseElement(QXmlStreamReader &xml) {
    const QString name = xml.name().toString().toLower();
    auto node = std::make_unique<Node>();
    node->elementName = name;

    if (name == QLatin1String("mi") || name == QLatin1String("mn") || name == QLatin1String("mo")
        || name == QLatin1String("mtext") || name == QLatin1String("ms")) {
        node->kind = Kind::Token;
        const auto attrs = xml.attributes();
        if (attrs.hasAttribute(QStringLiteral("mathvariant"))) {
            node->variant = parseVariant(attrs.value(QStringLiteral("mathvariant")));
        }
    } else if (name == QLatin1String("mrow") || name == QLatin1String("math")
               || name == QLatin1String("semantics")) {
        node->kind = Kind::Row;
    } else if (name == QLatin1String("msub")) {
        node->kind = Kind::Sub;
    } else if (name == QLatin1String("msup")) {
        node->kind = Kind::Sup;
    } else if (name == QLatin1String("msubsup")) {
        node->kind = Kind::SubSup;
    } else if (name == QLatin1String("mfrac")) {
        node->kind = Kind::Frac;
    } else if (name == QLatin1String("msqrt") || name == QLatin1String("mroot")) {
        node->kind = Kind::Sqrt;
    } else if (name == QLatin1String("munder")) {
        node->kind = Kind::Under;
    } else if (name == QLatin1String("mover")) {
        node->kind = Kind::Over;
    } else if (name == QLatin1String("munderover")) {
        node->kind = Kind::UnderOver;
    } else if (name == QLatin1String("mtable")) {
        node->kind = Kind::Table;
    } else if (name == QLatin1String("mtr") || name == QLatin1String("mlabeledtr")) {
        node->kind = Kind::TableRow;
    } else if (name == QLatin1String("mtd")) {
        node->kind = Kind::TableCell;
    } else if (name == QLatin1String("mfenced")) {
        node->kind = Kind::Fenced;
        const auto attrs = xml.attributes();
        // Per MathML spec these default to "(", ")", "," when missing.
        if (attrs.hasAttribute(QStringLiteral("open"))) {
            node->fenceOpen = attrs.value(QStringLiteral("open")).toString();
        } else {
            node->fenceOpen = QStringLiteral("(");
        }
        if (attrs.hasAttribute(QStringLiteral("close"))) {
            node->fenceClose = attrs.value(QStringLiteral("close")).toString();
        } else {
            node->fenceClose = QStringLiteral(")");
        }
        if (attrs.hasAttribute(QStringLiteral("separators"))) {
            node->fenceSeparators = attrs.value(QStringLiteral("separators")).toString();
        } else {
            node->fenceSeparators = QStringLiteral(",");
        }
    } else if (name == QLatin1String("mspace")) {
        node->kind = Kind::Space;
        const auto attrs = xml.attributes();
        if (attrs.hasAttribute(QStringLiteral("width"))) {
            const QString w = attrs.value(QStringLiteral("width")).toString();
            if (w.endsWith(QLatin1String("em"))) {
                node->spaceWidth = w.left(w.size() - 2).toDouble() * kBaseFontPx;
            } else if (w.endsWith(QLatin1String("px"))) {
                node->spaceWidth = w.left(w.size() - 2).toDouble();
            }
        }
    } else if (name == QLatin1String("annotation") || name == QLatin1String("annotation-xml")) {
        // Skip annotation contents entirely — they often contain MathType
        // MTEF gibberish or LaTeX markup that we don't want to render.
        int depth = 1;
        while (!xml.atEnd() && depth > 0) {
            xml.readNext();
            if (xml.isStartElement()) {
                ++depth;
            } else if (xml.isEndElement()) {
                --depth;
            }
        }
        return nullptr;
    } else {
        node->kind = Kind::Unknown;
    }

    return finishElement(xml, std::move(node));
}

}  // namespace

namespace {
QPixmap renderToCanvas(const QString &mathmlSource, int canvasW, int canvasH, qreal fontPx, bool darkTheme) {
    if (mathmlSource.trimmed().isEmpty() || canvasW <= 0 || canvasH <= 0) {
        return {};
    }
    QXmlStreamReader xml(mathmlSource);
    std::unique_ptr<Node> root;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            root = parseElement(xml);
            break;
        }
    }
    if (!root) {
        return {};
    }

    Ctx ctx{fontPx};
    Box box = layoutNode(*root, ctx);
    if (box.width <= 0 || box.height() <= 0) {
        return {};
    }

    const qreal margin = fontPx * 0.35;
    const qreal availW = canvasW - margin * 2;
    const qreal availH = canvasH - margin * 2;
    if (availW <= 0 || availH <= 0) {
        return {};
    }
    // Scale formula to fit the available area while preserving aspect.
    // Allow scaling up beyond 1.0 only when called from the preview dialog
    // path (where the caller picks a deliberately large fontPx and may want
    // to fill the canvas). For the card thumbnail path the caller picks
    // fontPx so the formula naturally fits — don't blow it up further.
    const qreal scaleW = availW / box.width;
    const qreal scaleH = availH / box.height();
    qreal scale = std::min(scaleW, scaleH);
    if (scale > 1.0) {
        scale = 1.0;
    }
    if (scale <= 0) {
        return {};
    }

    QPixmap pm(canvasW, canvasH);
    // Theme-appropriate background and ink. Dark mode uses a dark surface
    // matching the card body so the formula tile blends in.
    const QColor bg = darkTheme ? QColor(40, 42, 46) : Qt::white;
    const QColor fg = darkTheme ? QColor(232, 234, 237) : Qt::black;
    pm.fill(bg);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setPen(fg);

    const qreal scaledW = box.width * scale;
    const qreal scaledH = box.height() * scale;
    const qreal originX = (canvasW - scaledW) / 2.0;
    const qreal originY = (canvasH - scaledH) / 2.0;
    p.translate(originX, originY);
    if (scale != 1.0) {
        p.scale(scale, scale);
    }
    box.draw(p, 0, box.ascent);
    p.end();
    return pm;
}
}  // namespace

QPixmap MathMLRenderer::render(const QString &mathmlSource, bool darkTheme) {
    // Render at the canonical card preview dimensions. LocalSaver's load
    // path force-rescales thumbnails to this exact size with
    // Qt::IgnoreAspectRatio, so producing the thumbnail at this size up
    // front avoids the cross-restart stretch.
    return renderToCanvas(mathmlSource, kCardPreviewWidth, kCardPreviewHeight, kBaseFontPx, darkTheme);
}

QPixmap MathMLRenderer::renderAt(const QString &mathmlSource, const QSize &targetSize, bool darkTheme) {
    if (!targetSize.isValid() || targetSize.isEmpty()) {
        return render(mathmlSource, darkTheme);
    }
    // Pick a font size proportional to the canvas. The card path uses 24px
    // for a 275px-wide canvas; scale linearly so larger canvases get
    // proportionally larger glyphs. Cap min/max for sanity.
    const qreal fontPx = qBound<qreal>(16.0,
                                       kBaseFontPx * (targetSize.width() / qreal(kCardPreviewWidth)),
                                       96.0);
    return renderToCanvas(mathmlSource, targetSize.width(), targetSize.height(), fontPx, darkTheme);
}
