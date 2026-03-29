#include "ColorCardBody.h"

void ColorCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    const QColor color = ctx.card.color.isValid() ? ctx.card.color : QColor(QStringLiteral("#4A5F7A"));
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawRoundedRect(ctx.previewRect, 10.0, 10.0);
    QFont previewFont = painter->font();
    CardRenderUtils::applyUiFontDefaults(previewFont);
    previewFont.setPointSize(qMax(10, 12 * ctx.scale / 100));
    previewFont.setBold(true);
    const QColor fontColor(255 - color.red(), 255 - color.green(), 255 - color.blue());
    CardRenderUtils::drawElidedText(painter, ctx.previewRect,
                                    CardRenderUtils::previewTextForCard(ctx.card),
                                    previewFont, fontColor, Qt::AlignCenter);
}
