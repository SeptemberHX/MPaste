#include "RichTextCardBody.h"

#include "widget/ClipboardBoardModel.h"

#include <QPainterPath>

void RichTextCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    const bool shouldFallbackToText = ctx.card.previewKind != VisualPreview
        || (ctx.card.previewState == ClipboardBoardModel::PreviewUnavailable
            && ctx.card.thumbnail.isNull()
            && !ctx.card.normalizedText.trimmed().isEmpty());
    if (!shouldFallbackToText) {
        if (ctx.card.previewState == ClipboardBoardModel::PreviewReady && !ctx.card.thumbnail.isNull()) {
            // The preview is rendered card-wide: keep its scale and move it onto the
            // card's text edge (its own margin is ~4 px), clipping what runs past the right.
            // Scaled to the card's content width and set on its edges: nothing is cut
            // off, and the preview's own small page margin sits evenly left and right.
            const QPixmap &thumb = ctx.card.thumbnail;
            const qreal dpr = qMax<qreal>(1.0, thumb.devicePixelRatio());
            const QSizeF logical(thumb.width() / dpr, thumb.height() / dpr);
            const QRect content = ctx.previewRect;
            const qreal fit = content.width() / qMax<qreal>(1.0, logical.width());
            QPainterPath panel;
            panel.addRoundedRect(QRectF(content), 8, 8);
            painter->save();
            painter->setClipPath(panel, Qt::IntersectClip);
            painter->drawPixmap(QRectF(QPointF(content.topLeft()), logical * fit), thumb, QRectF(thumb.rect()));
            painter->restore();
        } else {
            CardRenderUtils::drawInsetVisualPreview(painter, ctx.bodyRect, ctx.card,
                                                    ctx.scale, ctx.darkTheme, ctx.loadingPhase, false);
        }
    } else {
        QFont previewFont = painter->font();
        const bool isCode = CardRenderUtils::looksLikeCode(ctx.card.normalizedText);
        if (isCode) {
            CardRenderUtils::paintCodeBlock(painter, ctx.bodyRect, CardRenderUtils::previewTextForCard(ctx.card),
                                            ctx.scale, ctx.darkTheme, ctx.bodyTextColor);
            return;
        }
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
                                         1.1);
    }
}
