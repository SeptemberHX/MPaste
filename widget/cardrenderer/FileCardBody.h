#ifndef MPASTE_FILECARDBODY_H
#define MPASTE_FILECARDBODY_H

#include "CardBodyRenderer.h"

class FileCardBody : public CardBodyRenderer {
public:
    void paint(QPainter *painter, const CardBodyContext &ctx) const override;
};

#endif // MPASTE_FILECARDBODY_H
