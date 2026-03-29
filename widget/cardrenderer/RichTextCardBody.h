#ifndef MPASTE_RICHTEXTCARDBODY_H
#define MPASTE_RICHTEXTCARDBODY_H

#include "CardBodyRenderer.h"

class RichTextCardBody : public CardBodyRenderer {
public:
    void paint(QPainter *painter, const CardBodyContext &ctx) const override;
};

#endif // MPASTE_RICHTEXTCARDBODY_H
