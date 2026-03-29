#ifndef MPASTE_LINKCARDBODY_H
#define MPASTE_LINKCARDBODY_H

#include "CardBodyRenderer.h"

class LinkCardBody : public CardBodyRenderer {
public:
    void paint(QPainter *painter, const CardBodyContext &ctx) const override;
};

#endif // MPASTE_LINKCARDBODY_H
