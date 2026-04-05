#include "RichTextCardBody.h"

#include "widget/ClipboardBoardModel.h"

void RichTextCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    const bool shouldFallbackToText = ctx.card.previewKind != VisualPreview
        || (ctx.card.previewState == ClipboardBoardModel::PreviewUnavailable
            && ctx.card.thumbnail.isNull()
            && !ctx.card.normalizedText.trimmed().isEmpty());
    if (!shouldFallbackToText) {
        if (ctx.card.previewState == ClipboardBoardModel::PreviewReady && !ctx.card.thumbnail.isNull()) {
            const QRect drawRect = ctx.bodyRect.adjusted(4, 0, -4, 0);
            CardRenderUtils::drawContainPixmap(painter, drawRect, ctx.card.thumbnail);
        } else {
            CardRenderUtils::drawManagedVisualPreview(painter, ctx.bodyRect, ctx.card,
                                                      ctx.scale, ctx.darkTheme, ctx.loadingPhase, false);
        }
    } else {
        QFont previewFont = painter->font();
        const bool isCode = CardRenderUtils::looksLikeCode(ctx.card.normalizedText);
        if (isCode) {
            CardRenderUtils::applyMonoFontDefaults(previewFont);
            previewFont.setPointSize(qMax(8, 10 * ctx.scale / 100));
        } else {
            CardRenderUtils::applyUiFontDefaults(previewFont);
            previewFont.setPointSize(qMax(9, 11 * ctx.scale / 100));
        }
        const int textPadX = qMax(6, 8 * ctx.scale / 100);
        const int textPadY = qMax(4, 6 * ctx.scale / 100);
        QRect textRect = ctx.bodyRect.adjusted(textPadX, textPadY, -textPadX, 0);
        if (textRect.width() <= 0 || textRect.height() <= 0) {
            textRect = ctx.bodyRect;
        }
        CardRenderUtils::drawWrappedText(painter, textRect,
                                         CardRenderUtils::previewTextForCard(ctx.card),
                                         previewFont, ctx.bodyTextColor,
                                         isCode ? 1.2 : 1.1);
    }
}
