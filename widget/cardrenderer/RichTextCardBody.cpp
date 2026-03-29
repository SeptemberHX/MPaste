#include "RichTextCardBody.h"

#include "widget/ClipboardBoardModel.h"

void RichTextCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    const bool shouldFallbackToText = ctx.card.previewKind != VisualPreview
        || (ctx.card.previewState == ClipboardBoardModel::PreviewUnavailable
            && ctx.card.thumbnail.isNull()
            && !ctx.card.normalizedText.trimmed().isEmpty());
    if (!shouldFallbackToText) {
        // Rich text thumbnails are rendered at exactly bodyRect size,
        // so draw directly without cover/contain scaling to avoid
        // any cropping of the left/right edges.
        if (ctx.card.previewState == ClipboardBoardModel::PreviewReady && !ctx.card.thumbnail.isNull()) {
            const QRect drawRect = ctx.bodyRect.adjusted(4, 0, -4, 0);
            painter->drawPixmap(drawRect.topLeft(), ctx.card.thumbnail);
        } else {
            CardRenderUtils::drawManagedVisualPreview(painter, ctx.bodyRect, ctx.card,
                                                      ctx.scale, ctx.darkTheme, ctx.loadingPhase, false);
        }
    } else {
        QFont previewFont = painter->font();
        CardRenderUtils::applyUiFontDefaults(previewFont);
        previewFont.setPointSize(qMax(9, 11 * ctx.scale / 100));
        const int textPadX = qMax(6, 8 * ctx.scale / 100);
        const int textPadY = qMax(4, 6 * ctx.scale / 100);
        QRect textRect = ctx.bodyRect.adjusted(textPadX, textPadY, -textPadX, 0);
        if (textRect.width() <= 0 || textRect.height() <= 0) {
            textRect = ctx.bodyRect;
        }
        CardRenderUtils::drawWrappedText(painter, textRect,
                                         CardRenderUtils::previewTextForCard(ctx.card),
                                         previewFont, ctx.bodyTextColor);
    }
}
