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
        paintAddress(painter, ctx, url);
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

void TextCardBody::paintAddress(QPainter *painter, const CardBodyContext &ctx, const QUrl &url) {
    const int padX = qMax(8, 12 * ctx.scale / 100);
    const int padY = qMax(6, 10 * ctx.scale / 100);
    const QRect area = ctx.bodyRect.adjusted(padX, padY, -padX, 0);

    QFont hostFont = painter->font();
    CardRenderUtils::applyUiFontDefaults(hostFont);
    hostFont.setPointSizeF(qMax(9.0, 11.0 * ctx.scale / 100));
    hostFont.setWeight(QFont::DemiBold);
    const int hostHeight = QFontMetrics(hostFont).height() + qMax(2, 4 * ctx.scale / 100);
    const QRect hostRect(area.left(), area.top(), area.width(), hostHeight);
    CardRenderUtils::drawElidedText(painter, hostRect, CardRenderUtils::displayHost(url), hostFont,
                                    ctx.bodyTextColor, Qt::AlignLeft | Qt::AlignVCenter);

    QString rest = url.toDisplayString(QUrl::RemoveScheme | QUrl::RemoveAuthority | QUrl::RemoveFragment);
    if (rest == QStringLiteral("/")) {
        rest.clear();
    }
    if (!rest.isEmpty()) {
        QFont pathFont = painter->font();
        CardRenderUtils::applyUiFontDefaults(pathFont);
        pathFont.setPointSize(qMax(8, 9 * ctx.scale / 100));
        // Break after separators so long paths wrap at sensible points.
        QString wrapped = rest;
        wrapped.replace(QLatin1Char('/'), QStringLiteral("/\u200B"));
        wrapped.replace(QLatin1Char('?'), QStringLiteral("\u200B?"));
        wrapped.replace(QLatin1Char('&'), QStringLiteral("\u200B&"));
        const QRect pathRect(area.left(), hostRect.bottom() + 1, area.width(),
                             qMax(0, area.bottom() - hostRect.bottom() - qMax(4, 6 * ctx.scale / 100)));
        CardRenderUtils::drawWrappedText(painter, pathRect, wrapped, pathFont, ctx.theme.footerTextColor, 1.15);
    }
}
