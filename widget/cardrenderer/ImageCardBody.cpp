#include "ImageCardBody.h"

void ImageCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    // Pictures fill the body edge to edge.
    CardRenderUtils::drawManagedVisualPreview(painter, ctx.bodyRect, ctx.card,
                                              ctx.scale, ctx.darkTheme, ctx.loadingPhase, false);
}
