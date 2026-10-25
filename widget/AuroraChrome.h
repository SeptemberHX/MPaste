// input: QPainter, a window rect, the theme and an accent colour.
// output: The Aurora window surface and the stylesheet its controls share.
// pos: Widget-layer look for MPaste's secondary windows (preview, OCR, settings,
//      details, alias, about), matching the Aurora cards and top bar.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_AURORACHROME_H
#define MPASTE_AURORACHROME_H

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRectF>
#include <QString>
#include <QTransform>

#include "WindowBlurHelper.h"

namespace AuroraChrome {

constexpr qreal kRadius = 20.0;

inline QColor ground(bool dark) {
    return dark ? QColor(QStringLiteral("#121319")) : QColor(QStringLiteral("#FBFBFD"));
}

// A rounded window with the accent as light from the top-left corner (wide
// across, shallow down), a hairline edge and a lit top rim.
inline void paintWindow(QPainter &p, const QRectF &widgetRect, bool dark, const QColor &accent,
                        qreal radius = kRadius) {
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(widgetRect, Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QRectF r = widgetRect.adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath body;
    body.addRoundedRect(r, radius, radius);
    QColor base = ground(dark);
    if (!WindowBlurHelper::useOpaqueFallback() && WindowBlurHelper::hasNativeBlur()) {
        base.setAlpha(dark ? 236 : 240);
    }
    p.fillPath(body, base);

    if (accent.isValid()) {
        // Reaches across to the right edge, still faintly lit there; the depth
        // down the window stays shallow.
        const qreal radiusX = r.width() * 1.08;
        const qreal radiusY = qMin(r.height() * 0.32, 180.0);
        QRadialGradient light(QPointF(0, 0), radiusX);
        QColor glow = accent;
        const int peak = dark ? 70 : 46;
        glow.setAlpha(peak);
        light.setColorAt(0.0, glow);
        glow.setAlpha(peak * 45 / 100);
        light.setColorAt(0.5, glow);
        glow.setAlpha(0);
        light.setColorAt(1.0, glow);
        QBrush brush(light);
        QTransform place;
        place.translate(r.left(), r.top());
        place.scale(1.0, radiusY / radiusX);
        brush.setTransform(place);
        p.save();
        p.setClipPath(body);
        p.fillRect(r, brush);
        p.restore();
    }

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 22), 1.0));
    p.drawPath(body);
    QLinearGradient rim(r.topLeft(), QPointF(r.left(), r.top() + 40));
    rim.setColorAt(0.0, QColor(255, 255, 255, dark ? 30 : 140));
    rim.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setPen(QPen(QBrush(rim), 1.0));
    p.drawPath(body);
}

// The backdrop under paintWindow's shape: call wherever the window used to ask
// for blur (after show and on theme changes). `inset` matches a paint rect inset.
inline void attachBackdrop(QWidget *window, bool dark, qreal inset = 0) {
    WindowBlurHelper::enableShapedBackdrop(window, dark, kRadius, inset);
}

// Controls by object name: auroraTitle / auroraSubtitle labels, auroraClose
// (round), auroraPrimary / auroraSecondary pills, auroraSegment groups of
// checkable pill buttons, auroraWell text areas, auroraInput fields,
// auroraPanel grouped rows, auroraSection headings, auroraHint small text.
inline QString styleSheet(bool dark) {
    const QString ink = dark ? QStringLiteral("#ECEDF2") : QStringLiteral("#1C1E25");
    const QString soft = dark ? QStringLiteral("rgba(255,255,255,140)") : QStringLiteral("rgba(28,30,37,150)");
    const QString faint = dark ? QStringLiteral("rgba(255,255,255,115)") : QStringLiteral("rgba(28,30,37,125)");
    const QString fill = dark ? QStringLiteral("rgba(255,255,255,18)") : QStringLiteral("rgba(0,0,0,13)");
    const QString fillHover = dark ? QStringLiteral("rgba(255,255,255,30)") : QStringLiteral("rgba(0,0,0,22)");
    const QString edge = dark ? QStringLiteral("rgba(255,255,255,16)") : QStringLiteral("rgba(0,0,0,16)");
    const QString well = dark ? QStringLiteral("rgba(0,0,0,82)") : QStringLiteral("rgba(255,255,255,200)");
    const QString panel = dark ? QStringLiteral("rgba(255,255,255,9)") : QStringLiteral("rgba(255,255,255,170)");
    const QString chosen = dark ? QStringLiteral("rgba(255,255,255,36)") : QStringLiteral("#FFFFFF");
    const QString primaryBg = dark ? QStringLiteral("#ECEDF2") : QStringLiteral("#1C1E25");
    const QString primaryInk = dark ? QStringLiteral("#121319") : QStringLiteral("#FFFFFF");
    return QStringLiteral(
        "QLabel#auroraTitle { color: %1; font-size: 15px; font-weight: 600; background: transparent; }"
        "QLabel#auroraSubtitle { color: %2; font-size: 12px; background: transparent; }"
        "QLabel#auroraHint { color: %3; font-size: 12px; background: transparent; }"
        "QLabel#auroraSection { color: %3; font-size: 12px; font-weight: 500; background: transparent; padding-left: 4px; }"
        // Padding and margin set too: without them a native style (Windows 11)
        // pads the button past 32 px and the title row clips it.
        "QToolButton#auroraClose { min-width: 32px; max-width: 32px; min-height: 32px; max-height: 32px;"
        "  padding: 0px; margin: 0px; border: none; border-radius: 15px; background: %4; color: %2; font-size: 15px; }"
        "QToolButton#auroraClose:hover { background: %5; color: %1; }"
        "QPushButton#auroraSecondary { min-height: 32px; max-height: 32px; padding: 0 14px; border: none;"
        "  border-radius: 15px; background: %4; color: %1; font-size: 13px; }"
        "QPushButton#auroraSecondary:hover { background: %5; }"
        "QPushButton#auroraPrimary { min-height: 32px; max-height: 32px; padding: 0 16px; border: none;"
        "  border-radius: 15px; background: %9; color: %10; font-size: 13px; font-weight: 600; }"
        "QPushButton#auroraPrimary:hover { background: %9; }"
        "QWidget#auroraSegment { background: %4; border: 1px solid %6; border-radius: 15px; }"
        "QWidget#auroraSegment QToolButton, QWidget#auroraSegment QPushButton { min-height: 28px; max-height: 28px;"
        "  padding: 0 13px; border: none; border-radius: 13px; background: transparent; color: %2; font-size: 13px; }"
        "QWidget#auroraSegment QToolButton:hover, QWidget#auroraSegment QPushButton:hover { color: %1; }"
        "QWidget#auroraSegment QToolButton:checked, QWidget#auroraSegment QPushButton:checked {"
        "  background: %8; color: %1; font-weight: 600; }"
        "QTextBrowser#auroraWell, QTextEdit#auroraWell, QPlainTextEdit#auroraWell, QScrollArea#auroraWell {"
        "  background: %7; border: 1px solid %6; border-radius: 14px; color: %1; padding: 8px 10px;"
        "  selection-background-color: rgba(91,141,239,150); }"
        "QLineEdit#auroraInput { min-height: 38px; max-height: 38px; padding: 0 16px; border: none; border-radius: 18px;"
        "  background: %4; color: %1; font-size: 14px; selection-background-color: rgba(91,141,239,150); }"
        "QLineEdit#auroraInput:focus { border: 1.5px solid rgba(91,141,239,205); }"
        "QFrame#auroraPanel { background: %11; border: 1px solid %6; border-radius: 16px; }")
        .arg(ink, soft, faint, fill, fillHover, edge, well, chosen, primaryBg, primaryInk, panel);
}

} // namespace AuroraChrome

#endif // MPASTE_AURORACHROME_H
