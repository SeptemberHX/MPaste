#ifndef MPASTE_COLORCARDBODY_H
#define MPASTE_COLORCARDBODY_H

#include "CardBodyRenderer.h"

class ColorCardBody : public CardBodyRenderer {
public:
    void paint(QPainter *painter, const CardBodyContext &ctx) const override;

    // A copied colour value such as "#4A90E2" or "rgb(74, 144, 226)"; invalid otherwise.
    static QColor colorFromText(const QString &text);
    // Swatch on top, the value and its RGB below.
    static void paintSwatch(QPainter *painter, const CardBodyContext &ctx, const QColor &color, const QString &label);
};

#endif // MPASTE_COLORCARDBODY_H
