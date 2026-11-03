#ifndef MPASTE_LINKCARDBODY_H
#define MPASTE_LINKCARDBODY_H

#include "CardBodyRenderer.h"

class LinkCardBody : public CardBodyRenderer {
public:
    void paint(QPainter *painter, const CardBodyContext &ctx) const override;
    // Laid out as a picture: its page's picture is saved.
    static bool showsPagePicture(const CardData &card);
};

#endif // MPASTE_LINKCARDBODY_H
