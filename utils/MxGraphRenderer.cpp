// input: Depends on MxGraphRenderer.h, Qt XML/painting APIs.
// output: Implements lightweight mxGraphModel XML to QImage rendering.
// pos: utils layer draw.io diagram renderer implementation.
// update: If I change, update this header block and my folder README.md.
#include "MxGraphRenderer.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

#include <cmath>

namespace MxGraphRenderer {

namespace {

struct MxStyle {
    QString shape;
    QColor fillColor{238, 238, 238};
    QColor strokeColor{60, 60, 60};
    QColor fontColor{40, 40, 40};
    qreal strokeWidth = 1.2;
    int fontSize = 11;
    int fontStyle = 0; // 1=bold 2=italic 4=underline
    bool rounded = false;
    bool dashed = false;
    bool noStroke = false;
    bool noFill = false;
    bool fillExplicit = false;
    bool strokeExplicit = false;
    bool fontColorExplicit = false;
    QString align = QStringLiteral("center");
    QString verticalAlign = QStringLiteral("middle");
};

// Adapt a style for dark mode: invert colors that are too dark to
// read on a dark card background.
void adaptStyleForDarkMode(MxStyle &s) {
    auto lightenIfDark = [](QColor &c) {
        if (c.alpha() == 0) return;
        // If the color's lightness is below 50%, invert it.
        if (c.lightnessF() < 0.5) {
            c = QColor::fromHslF(c.hslHueF(), c.hslSaturationF(),
                                 1.0 - c.lightnessF(), c.alphaF());
        }
    };
    if (!s.fontColorExplicit || s.fontColor.lightnessF() < 0.35) {
        lightenIfDark(s.fontColor);
    }
    if (!s.strokeExplicit || s.strokeColor.lightnessF() < 0.35) {
        lightenIfDark(s.strokeColor);
    }
    if (!s.fillExplicit) {
        s.fillColor = QColor(58, 60, 64);
    }
}

struct MxCell {
    QString id;
    QString parentId;
    QString value;
    MxStyle style;
    QRectF geometry;
    bool isEdge = false;
    QString sourceId;
    QString targetId;
    QList<QPointF> edgePoints;
};

MxStyle parseStyle(const QString &styleStr) {
    MxStyle s;
    const QStringList parts = styleStr.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString &part = parts[i];
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq < 0) {
            if (i == 0) {
                s.shape = part.trimmed().toLower();
            }
            continue;
        }
        const QString key = part.left(eq).trimmed().toLower();
        const QString val = part.mid(eq + 1).trimmed();
        if (key == QLatin1String("fillcolor")) {
            s.fillExplicit = true;
            if (val.toLower() == QLatin1String("none")) {
                s.noFill = true;
            } else {
                s.fillColor = QColor(val);
            }
        } else if (key == QLatin1String("strokecolor")) {
            s.strokeExplicit = true;
            if (val.toLower() == QLatin1String("none")) {
                s.noStroke = true;
            } else {
                s.strokeColor = QColor(val);
            }
        } else if (key == QLatin1String("fontcolor")) {
            s.fontColorExplicit = true;
            s.fontColor = QColor(val);
        } else if (key == QLatin1String("strokewidth")) {
            s.strokeWidth = val.toDouble();
        } else if (key == QLatin1String("fontsize")) {
            s.fontSize = val.toInt();
        } else if (key == QLatin1String("fontstyle")) {
            s.fontStyle = val.toInt();
        } else if (key == QLatin1String("rounded")) {
            s.rounded = (val == QLatin1String("1"));
        } else if (key == QLatin1String("dashed")) {
            s.dashed = (val == QLatin1String("1"));
        } else if (key == QLatin1String("align")) {
            s.align = val.toLower();
        } else if (key == QLatin1String("verticalalign")) {
            s.verticalAlign = val.toLower();
        }
    }
    return s;
}

QString decodeHtmlEntities(const QString &text) {
    QString result = text;
    result.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    result.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    result.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    result.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    result.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    // Strip remaining HTML tags (e.g. <br>, <b>, etc.)
    static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
    result.replace(tagRe, QStringLiteral("\n"));
    // Collapse multiple newlines.
    static const QRegularExpression nlRe(QStringLiteral("\\n{2,}"));
    result.replace(nlRe, QStringLiteral("\n"));
    return result.trimmed();
}

QList<MxCell> parseMxGraphXml(const QString &xml) {
    QList<MxCell> cells;
    QXmlStreamReader reader(xml);

    MxCell current;
    bool inCell = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QLatin1String("mxCell")) {
                inCell = true;
                current = MxCell();
                const QXmlStreamAttributes attrs = reader.attributes();
                current.id = attrs.value(QLatin1String("id")).toString();
                current.parentId = attrs.value(QLatin1String("parent")).toString();
                current.value = attrs.value(QLatin1String("value")).toString();
                current.isEdge = (attrs.value(QLatin1String("edge")) == QLatin1String("1"));
                current.sourceId = attrs.value(QLatin1String("source")).toString();
                current.targetId = attrs.value(QLatin1String("target")).toString();
                if (attrs.hasAttribute(QLatin1String("style"))) {
                    current.style = parseStyle(attrs.value(QLatin1String("style")).toString());
                }
                if (attrs.hasAttribute(QLatin1String("vertex"))
                    || attrs.hasAttribute(QLatin1String("edge"))) {
                    // Will be added when geometry is found or at end element.
                }
            } else if (reader.name() == QLatin1String("mxGeometry") && inCell) {
                const QXmlStreamAttributes attrs = reader.attributes();
                const qreal x = attrs.value(QLatin1String("x")).toDouble();
                const qreal y = attrs.value(QLatin1String("y")).toDouble();
                const qreal w = attrs.value(QLatin1String("width")).toDouble();
                const qreal h = attrs.value(QLatin1String("height")).toDouble();
                current.geometry = QRectF(x, y, w, h);
            } else if (reader.name() == QLatin1String("mxPoint") && inCell) {
                const QXmlStreamAttributes attrs = reader.attributes();
                const qreal px = attrs.value(QLatin1String("x")).toDouble();
                const qreal py = attrs.value(QLatin1String("y")).toDouble();
                current.edgePoints.append(QPointF(px, py));
            }
        } else if (reader.isEndElement()) {
            if (reader.name() == QLatin1String("mxCell") && inCell) {
                inCell = false;
                // Skip root cells (id 0 and 1) that have no visual representation.
                if (current.id != QLatin1String("0") && current.id != QLatin1String("1")) {
                    cells.append(current);
                }
            }
        }
    }
    return cells;
}

QRectF computeBoundingBox(const QList<MxCell> &cells) {
    QRectF bbox;
    for (const MxCell &cell : cells) {
        if (cell.geometry.isNull() && cell.edgePoints.isEmpty()) {
            continue;
        }
        if (!cell.geometry.isNull()) {
            bbox = bbox.isNull() ? cell.geometry : bbox.united(cell.geometry);
        }
        for (const QPointF &pt : cell.edgePoints) {
            if (bbox.isNull()) {
                bbox = QRectF(pt, QSizeF(1, 1));
            } else {
                bbox = bbox.united(QRectF(pt, QSizeF(1, 1)));
            }
        }
    }
    return bbox;
}

void drawShape(QPainter &painter, const MxCell &cell, const MxStyle &s) {
    const QRectF &r = cell.geometry;
    if (r.isNull()) {
        return;
    }

    // Fill
    if (!s.noFill) {
        painter.setBrush(s.fillColor);
    } else {
        painter.setBrush(Qt::NoBrush);
    }

    // Stroke
    if (!s.noStroke) {
        QPen pen(s.strokeColor, s.strokeWidth);
        if (s.dashed) {
            pen.setStyle(Qt::DashLine);
        }
        painter.setPen(pen);
    } else {
        painter.setPen(Qt::NoPen);
    }

    // Shape
    if (s.shape == QLatin1String("ellipse")) {
        painter.drawEllipse(r);
    } else if (s.shape == QLatin1String("rhombus")) {
        QPainterPath path;
        path.moveTo(r.center().x(), r.top());
        path.lineTo(r.right(), r.center().y());
        path.lineTo(r.center().x(), r.bottom());
        path.lineTo(r.left(), r.center().y());
        path.closeSubpath();
        painter.drawPath(path);
    } else if (s.shape == QLatin1String("triangle")) {
        QPainterPath path;
        path.moveTo(r.center().x(), r.top());
        path.lineTo(r.right(), r.bottom());
        path.lineTo(r.left(), r.bottom());
        path.closeSubpath();
        painter.drawPath(path);
    } else if (s.shape == QLatin1String("cylinder")
               || s.shape == QLatin1String("cylinder3")) {
        const qreal capH = qMin(r.height() * 0.15, 12.0);
        QPainterPath path;
        path.moveTo(r.left(), r.top() + capH);
        path.cubicTo(r.left(), r.top(), r.right(), r.top(),
                     r.right(), r.top() + capH);
        path.lineTo(r.right(), r.bottom() - capH);
        path.cubicTo(r.right(), r.bottom(), r.left(), r.bottom(),
                     r.left(), r.bottom() - capH);
        path.closeSubpath();
        painter.drawPath(path);
        // Top cap
        QPainterPath cap;
        cap.moveTo(r.left(), r.top() + capH);
        cap.cubicTo(r.left(), r.top() + capH * 2, r.right(), r.top() + capH * 2,
                    r.right(), r.top() + capH);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(cap);
    } else if (s.shape == QLatin1String("hexagon")) {
        const qreal inset = r.width() * 0.2;
        QPainterPath path;
        path.moveTo(r.left() + inset, r.top());
        path.lineTo(r.right() - inset, r.top());
        path.lineTo(r.right(), r.center().y());
        path.lineTo(r.right() - inset, r.bottom());
        path.lineTo(r.left() + inset, r.bottom());
        path.lineTo(r.left(), r.center().y());
        path.closeSubpath();
        painter.drawPath(path);
    } else if (s.shape == QLatin1String("text")) {
        // Text-only: no border or fill.
    } else {
        // Default: rectangle.
        if (s.rounded) {
            const qreal radius = qMin(r.width(), r.height()) * 0.12;
            painter.drawRoundedRect(r, radius, radius);
        } else {
            painter.drawRect(r);
        }
    }

    // Label text
    const QString label = decodeHtmlEntities(cell.value);
    if (!label.isEmpty()) {
        QFont font;
        font.setPixelSize(qMax(8, s.fontSize));
        if (s.fontStyle & 1) font.setBold(true);
        if (s.fontStyle & 2) font.setItalic(true);
        if (s.fontStyle & 4) font.setUnderline(true);
        painter.setFont(font);
        painter.setPen(s.fontColor);

        int flags = Qt::TextWordWrap;
        if (s.align == QLatin1String("left")) flags |= Qt::AlignLeft;
        else if (s.align == QLatin1String("right")) flags |= Qt::AlignRight;
        else flags |= Qt::AlignHCenter;

        if (s.verticalAlign == QLatin1String("top")) flags |= Qt::AlignTop;
        else if (s.verticalAlign == QLatin1String("bottom")) flags |= Qt::AlignBottom;
        else flags |= Qt::AlignVCenter;

        const QRectF textRect = r.adjusted(3, 2, -3, -2);
        painter.drawText(textRect, flags, label);
    }
}

void drawEdge(QPainter &painter, const MxCell &cell,
              const QHash<QString, QRectF> &cellGeometries) {
    const MxStyle &s = cell.style;

    QPen pen(s.strokeColor, s.strokeWidth);
    if (s.dashed) {
        pen.setStyle(Qt::DashLine);
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Collect endpoints.  When both source and target cells are known,
    // draw a straight line between them — the stored waypoints are
    // auto-routing artifacts (often relative offsets) that cause
    // unwanted bends.
    QList<QPointF> points;
    const bool hasSource = !cell.sourceId.isEmpty() && cellGeometries.contains(cell.sourceId);
    const bool hasTarget = !cell.targetId.isEmpty() && cellGeometries.contains(cell.targetId);

    if (hasSource) {
        points.append(cellGeometries[cell.sourceId].center());
    }
    if (hasSource && hasTarget) {
        // Straight line — skip intermediate waypoints.
    } else {
        points.append(cell.edgePoints);
    }
    if (hasTarget) {
        points.append(cellGeometries[cell.targetId].center());
    }

    if (points.size() < 2) {
        return;
    }

    for (int i = 0; i < points.size() - 1; ++i) {
        painter.drawLine(points[i], points[i + 1]);
    }

    // Arrowhead at end.
    const QPointF &p1 = points[points.size() - 2];
    const QPointF &p2 = points.last();
    const qreal angle = std::atan2(p2.y() - p1.y(), p2.x() - p1.x());
    const qreal arrowLen = 8.0;
    const qreal arrowAngle = 0.45;
    const QPointF a1(p2.x() - arrowLen * std::cos(angle - arrowAngle),
                     p2.y() - arrowLen * std::sin(angle - arrowAngle));
    const QPointF a2(p2.x() - arrowLen * std::cos(angle + arrowAngle),
                     p2.y() - arrowLen * std::sin(angle + arrowAngle));
    painter.setBrush(s.strokeColor);
    QPainterPath arrow;
    arrow.moveTo(p2);
    arrow.lineTo(a1);
    arrow.lineTo(a2);
    arrow.closeSubpath();
    painter.drawPath(arrow);

    // Edge label
    if (!cell.value.isEmpty()) {
        const QString label = decodeHtmlEntities(cell.value);
        QPointF mid = points[points.size() / 2];
        if (points.size() >= 2) {
            mid = (points[points.size() / 2 - 1] + points[points.size() / 2]) / 2.0;
        }
        QFont font;
        font.setPixelSize(qMax(8, s.fontSize));
        painter.setFont(font);
        painter.setPen(s.fontColor);
        const QFontMetricsF fm(font);
        const QRectF textRect(mid.x() - 50, mid.y() - fm.height(), 100, fm.height() * 2);
        painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, label);
    }
}

} // anonymous namespace

bool isMxGraphContent(const QString &text) {
    return text.contains(QLatin1String("mxGraphModel"))
        || text.contains(QLatin1String("mxCell"))
        || text.contains(QLatin1String("%3CmxGraphModel%3E"));
}

QString extractMxGraphXml(const QString &text) {
    // Try URL-encoded form first.
    int start = text.indexOf(QLatin1String("%3CmxGraphModel%3E"));
    if (start >= 0) {
        int end = text.indexOf(QLatin1String("%3C%2FmxGraphModel%3E"), start);
        if (end >= 0) {
            end += 21; // length of "%3C%2FmxGraphModel%3E"
            return QUrl::fromPercentEncoding(text.mid(start, end - start).toUtf8());
        }
    }

    // Try raw XML.
    start = text.indexOf(QLatin1String("<mxGraphModel"));
    if (start >= 0) {
        int end = text.indexOf(QLatin1String("</mxGraphModel>"), start);
        if (end >= 0) {
            return text.mid(start, end - start + 15);
        }
    }

    return {};
}

QImage render(const QString &mxGraphXml, const QSize &pixelSize, qreal devicePixelRatio, bool darkMode) {
    if (mxGraphXml.isEmpty() || !pixelSize.isValid()) {
        return {};
    }

    QList<MxCell> cells = parseMxGraphXml(mxGraphXml);
    if (cells.isEmpty()) {
        return {};
    }

    if (darkMode) {
        for (MxCell &cell : cells) {
            adaptStyleForDarkMode(cell.style);
        }
    }

    const QRectF bbox = computeBoundingBox(cells);
    if (bbox.isNull()) {
        return {};
    }

    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(darkMode ? QColor(42, 44, 48) : QColor(243, 243, 243));
    image.setDevicePixelRatio(devicePixelRatio);

    const QSizeF logicalSize(pixelSize.width() / devicePixelRatio,
                             pixelSize.height() / devicePixelRatio);

    // Compute scale to fit bounding box into the image with padding.
    const qreal pad = 12.0;
    const qreal availW = logicalSize.width() - pad * 2;
    const qreal availH = logicalSize.height() - pad * 2;
    const qreal scaleX = (bbox.width() > 0) ? availW / bbox.width() : 1.0;
    const qreal scaleY = (bbox.height() > 0) ? availH / bbox.height() : 1.0;
    const qreal scale = qMin(scaleX, scaleY);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // Center the diagram in the image.
    const qreal scaledW = bbox.width() * scale;
    const qreal scaledH = bbox.height() * scale;
    const qreal offsetX = pad + (availW - scaledW) / 2.0 - bbox.left() * scale;
    const qreal offsetY = pad + (availH - scaledH) / 2.0 - bbox.top() * scale;
    painter.translate(offsetX, offsetY);
    painter.scale(scale, scale);

    // Build geometry lookup for edge routing.
    QHash<QString, QRectF> cellGeometries;
    for (const MxCell &cell : cells) {
        if (!cell.isEdge && !cell.geometry.isNull()) {
            cellGeometries[cell.id] = cell.geometry;
        }
    }

    // Draw edges first (behind shapes).
    for (const MxCell &cell : cells) {
        if (cell.isEdge) {
            drawEdge(painter, cell, cellGeometries);
        }
    }

    // Draw shapes.
    for (const MxCell &cell : cells) {
        if (!cell.isEdge) {
            drawShape(painter, cell, cell.style);
        }
    }

    painter.end();
    return image;
}

} // namespace MxGraphRenderer
