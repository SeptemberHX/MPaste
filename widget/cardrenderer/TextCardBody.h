#ifndef MPASTE_TEXTCARDBODY_H
#define MPASTE_TEXTCARDBODY_H

#include "CardBodyRenderer.h"

class TextCardBody : public CardBodyRenderer {
public:
    void paint(QPainter *painter, const CardBodyContext &ctx) const override;
};

#endif // MPASTE_TEXTCARDBODY_H
