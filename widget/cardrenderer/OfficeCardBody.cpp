#include "OfficeCardBody.h"

void OfficeCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    // If a thumbnail is available, show the visual preview.
    if (!ctx.card.thumbnail.isNull()
        || ctx.card.previewState == ClipboardBoardModel::PreviewLoading) {
        CardRenderUtils::drawManagedVisualPreview(painter, ctx.bodyRect, ctx.card,
                                                  ctx.scale, ctx.darkTheme, ctx.loadingPhase, true);
        return;
    }

    // Fallback to text preview (e.g. MathType equations without image data).
    const QString text = CardRenderUtils::previewTextForCard(ctx.card);
    if (!text.isEmpty()) {
        QFont previewFont = painter->font();
        CardRenderUtils::applyUiFontDefaults(previewFont);
        previewFont.setPointSize(qMax(9, 11 * ctx.scale / 100));
        const int pad = qMax(6, 8 * ctx.scale / 100);
        QRect textRect = ctx.bodyRect.adjusted(pad, pad, -pad, 0);
        if (textRect.width() > 0 && textRect.height() > 0) {
            CardRenderUtils::drawWrappedText(painter, textRect, text,
                                             previewFont, ctx.bodyTextColor);
        }
        return;
    }

    CardRenderUtils::drawManagedVisualPreview(painter, ctx.bodyRect, ctx.card,
                                              ctx.scale, ctx.darkTheme, ctx.loadingPhase, true);
}
