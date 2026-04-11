#ifndef MPASTE_SURFACEPAINTER_H
#define MPASTE_SURFACEPAINTER_H

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include "WindowBlurHelper.h"

// Shared "frosted rounded panel" painting used by TodoWidget, SettingsDialog,
// AboutDialog and ReminderToast. Centralises the CompositionMode_Clear →
// rounded fill → luminous-edge sequence so Ubuntu opaque-fallback changes
// only need to be made in one place.
namespace SurfacePainter {

inline QColor bodyColor(bool dark, bool opaque,
                        int translucentAlpha, int opaqueAlpha = 236) {
    if (dark)
        return opaque ? QColor(24, 31, 43, opaqueAlpha)
                      : QColor(18, 24, 36, translucentAlpha);
    // Light mode: a soft cool-white tint. Alpha needs to be noticeably
    // higher than dark mode to read as "frosted glass" on light desktops.
    return opaque ? QColor(245, 248, 252, opaqueAlpha)
                  : QColor(240, 245, 252, translucentAlpha);
}

inline QColor edgeColor(bool dark, bool opaque, int translucentAlpha) {
    if (dark)
        return opaque ? QColor(255, 255, 255, 72)
                      : QColor(255, 255, 255, translucentAlpha);
    // Light mode: dark edge so the border is visible on light backgrounds.
    return opaque ? QColor(0, 0, 0, 35)
                  : QColor(0, 0, 0, qMax(8, translucentAlpha / 3));
}

// Clear the widget rect to transparent, then paint a rounded-rect body
// with a thin luminous edge. `p` must already have Antialiasing enabled.
inline void paintPanel(QPainter &p, const QRectF &widgetRect,
                       qreal radius, bool dark,
                       int bodyAlpha, int edgeAlpha = 45,
                       int opaqueBodyAlpha = 236,
                       qreal edgeWidth = 1.0) {
    const bool opaque = WindowBlurHelper::useOpaqueFallback();

    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(widgetRect, Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QRectF r = widgetRect.adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath body;
    body.addRoundedRect(r, radius, radius);
    // Light mode needs more body alpha to be visible on light desktops.
    const int effectiveBody = dark ? bodyAlpha : qMax(bodyAlpha, 60);
    p.fillPath(body, bodyColor(dark, opaque, effectiveBody, opaqueBodyAlpha));

    QPen edge(edgeColor(dark, opaque, edgeAlpha));
    edge.setWidthF(edgeWidth);
    p.setPen(edge);
    p.setBrush(Qt::NoBrush);
    p.drawPath(body);
}

} // namespace SurfacePainter

#endif
