#ifndef MPASTE_OFFICECARDBODY_H
#define MPASTE_OFFICECARDBODY_H

#include "CardBodyRenderer.h"

class OfficeCardBody : public CardBodyRenderer {
public:
    void paint(QPainter *painter, const CardBodyContext &ctx) const override;
};

#endif // MPASTE_OFFICECARDBODY_H
