#ifndef MPASTE_IMAGECARDBODY_H
#define MPASTE_IMAGECARDBODY_H

#include "CardBodyRenderer.h"

class ImageCardBody : public CardBodyRenderer {
public:
    void paint(QPainter *painter, const CardBodyContext &ctx) const override;
};

#endif // MPASTE_IMAGECARDBODY_H
