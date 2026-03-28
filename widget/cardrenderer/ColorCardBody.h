#ifndef MPASTE_COLORCARDBODY_H
#define MPASTE_COLORCARDBODY_H

#include "CardBodyRenderer.h"

class ColorCardBody : public CardBodyRenderer {
public:
    void paint(QPainter *painter, const CardBodyContext &ctx) const override;
};

#endif // MPASTE_COLORCARDBODY_H
