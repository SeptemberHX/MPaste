#include "OfficeCardBody.h"

void OfficeCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    const QRect imagePreviewRect = ctx.bodyRect;
    CardRenderUtils::drawManagedVisualPreview(painter, imagePreviewRect, ctx.card,
                                              ctx.scale, ctx.darkTheme, ctx.loadingPhase, true);
}
