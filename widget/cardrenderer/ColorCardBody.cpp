#include "ColorCardBody.h"

#include <QRegularExpression>

void ColorCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    const QString label = CardRenderUtils::previewTextForCard(ctx.card).trimmed();
    QColor color = ctx.card.color;
    if (!color.isValid()) {
        color = colorFromText(label);
    }
    paintSwatch(painter, ctx, color.isValid() ? color : QColor(QStringLiteral("#4A5F7A")), label);
}

QColor ColorCardBody::colorFromText(const QString &text) {
    const QString value = text.trimmed();
    if (value.size() > 40 || value.contains(QLatin1Char('\n'))) {
        return {};
    }
    static const QRegularExpression hex(QStringLiteral("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    if (hex.match(value).hasMatch()) {
        return QColor::fromString(value);
    }
    static const QRegularExpression rgb(QStringLiteral(
        "^rgba?\\(\\s*(\\d{1,3})\\s*[, ]\\s*(\\d{1,3})\\s*[, ]\\s*(\\d{1,3})\\s*(?:[,/]\\s*([0-9.]+%?)\\s*)?\\)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = rgb.match(value);
    if (match.hasMatch()) {
        const int r = match.captured(1).toInt();
        const int g = match.captured(2).toInt();
        const int b = match.captured(3).toInt();
        if (r <= 255 && g <= 255 && b <= 255) {
            return QColor(r, g, b);
        }
    }
    return {};
}

void ColorCardBody::paintSwatch(QPainter *painter, const CardBodyContext &ctx, const QColor &color, const QString &label) {
    const int padX = qMax(8, 12 * ctx.scale / 100);
    const int padTop = qMax(6, 10 * ctx.scale / 100);
    const int textBlock = qMax(36, 46 * ctx.scale / 100);
    const QRect area = ctx.bodyRect.adjusted(padX, padTop, -padX, -qMax(2, 4 * ctx.scale / 100));
    const QRect swatchRect(area.left(), area.top(), area.width(), qMax(0, area.height() - textBlock));
    const qreal radius = qMax(6, 9 * ctx.scale / 100);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(color);
    // A hairline keeps colours close to the card's own tone visible.
    QColor edge = ctx.darkTheme ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 22);
    painter->setPen(QPen(edge, 1));
    painter->drawRoundedRect(QRectF(swatchRect).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter->restore();

    QFont valueFont = painter->font();
    CardRenderUtils::applyMonoFontDefaults(valueFont);
    valueFont.setPointSize(qMax(8, 10 * ctx.scale / 100));
    valueFont.setWeight(QFont::DemiBold);
    QFont detailFont = painter->font();
    CardRenderUtils::applyUiFontDefaults(detailFont);
    detailFont.setPointSize(qMax(7, 8 * ctx.scale / 100));

    const int lineHeight = textBlock / 2;
    const QRect valueRect(area.left(), swatchRect.bottom() + 1 + qMax(4, 6 * ctx.scale / 100), area.width(), lineHeight);
    const QRect detailRect(area.left(), valueRect.bottom() + 1, area.width(), lineHeight - qMax(4, 6 * ctx.scale / 100));
    const QString value = label.isEmpty() ? color.name(QColor::HexRgb).toUpper() : label;
    CardRenderUtils::drawElidedText(painter, valueRect, value, valueFont, ctx.bodyTextColor, Qt::AlignLeft | Qt::AlignVCenter);
    const QString detail = QStringLiteral("RGB %1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue());
    CardRenderUtils::drawElidedText(painter, detailRect, detail, detailFont, ctx.theme.footerTextColor, Qt::AlignLeft | Qt::AlignVCenter);
}
