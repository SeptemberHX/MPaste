#include "TextCardBody.h"
#include "ColorCardBody.h"

void TextCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    // A copied colour value reads better as a swatch than as seven characters.
    const QColor color = ColorCardBody::colorFromText(ctx.card.normalizedText);
    if (color.isValid()) {
        ColorCardBody::paintSwatch(painter, ctx, color, ctx.card.normalizedText.trimmed());
        return;
    }
    // A bare address: the site first, the path quieter below it.
    const QUrl url = CardRenderUtils::singleWebUrl(ctx.card.normalizedText);
    if (url.isValid()) {
        CardRenderUtils::paintWebAddress(painter, ctx, url);
        return;
    }
    if (CardRenderUtils::looksLikeCode(ctx.card.normalizedText)) {
        CardRenderUtils::paintCodeBlock(painter, ctx.bodyRect, CardRenderUtils::previewTextForCard(ctx.card),
                                        ctx.scale, ctx.darkTheme, ctx.bodyTextColor);
        return;
    }
    QFont previewFont = painter->font();
    CardRenderUtils::applyUiFontDefaults(previewFont);
    previewFont.setPointSize(qMax(8, 10 * ctx.scale / 100));
    // Same left edge as the header icon and the footer.
    const int textPadX = qMax(8, 12 * ctx.scale / 100);
    const int textPadY = qMax(6, 8 * ctx.scale / 100);
    QRect textRect = ctx.bodyRect.adjusted(textPadX, textPadY, -textPadX, 0);
    if (textRect.width() <= 0 || textRect.height() <= 0) {
        textRect = ctx.bodyRect;
    }
    CardRenderUtils::drawWrappedText(painter, textRect,
                                     CardRenderUtils::previewTextForCard(ctx.card),
                                     previewFont, ctx.bodyTextColor,
                                     1.2);
}
