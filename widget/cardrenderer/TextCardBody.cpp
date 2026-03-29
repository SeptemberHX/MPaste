#include "TextCardBody.h"

void TextCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
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
                                     isCode ? 1.2 : 0);
}
