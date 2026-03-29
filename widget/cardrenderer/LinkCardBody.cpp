#include "LinkCardBody.h"

#include <QSet>
#include <QUrl>

namespace {

bool isLikelyLinkPreviewImage(const QPixmap &pixmap) {
    if (pixmap.isNull()) {
        return false;
    }

    const qreal dpr = qMax<qreal>(1.0, pixmap.devicePixelRatio());
    const QSize logicalSize(qRound(pixmap.width() / dpr), qRound(pixmap.height() / dpr));
    if (!logicalSize.isValid()) {
        return false;
    }

    const int maxDim = qMax(logicalSize.width(), logicalSize.height());
    if (maxDim < 96) {
        return false;
    }

    const qreal aspect = logicalSize.width() / qMax(1.0, static_cast<qreal>(logicalSize.height()));
    const bool squareish = aspect >= 0.85 && aspect <= 1.18;
    if (!squareish) {
        return true;
    }

    static const QSet<int> kCommonIconSizes = {
        16, 24, 32, 48, 64, 96, 128, 192, 256, 512
    };
    if (kCommonIconSizes.contains(maxDim)) {
        return false;
    }
    return maxDim >= 320;
}

} // anonymous namespace

void LinkCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    QUrl currentUrl;
    if (!ctx.card.url.isEmpty()) {
        currentUrl = QUrl(ctx.card.url);
    } else if (!ctx.card.normalizedUrls.isEmpty()) {
        currentUrl = ctx.card.normalizedUrls.first();
    } else {
        currentUrl = QUrl(ctx.card.normalizedText.left(512).trimmed());
    }

    if (!ctx.card.thumbnail.isNull() && isLikelyLinkPreviewImage(ctx.card.thumbnail)) {
        // Real og:image -- draw as cover.
        CardRenderUtils::drawCoverPixmap(painter, ctx.bodyRect, ctx.card.thumbnail, ctx.card.name, ctx.card.imageSize);
    } else {
        // Browser-window style fallback preview fills the entire body.
        // Pass favicon: prefer card.favicon, fall back to thumbnail if
        // it's a small icon-like image.
        const QPixmap &faviconForPreview = !ctx.card.favicon.isNull() ? ctx.card.favicon : ctx.card.thumbnail;
        const QPixmap fallbackPreview = ctx.delegate->linkFallbackPreview(currentUrl, ctx.card.title, ctx.bodyRect.size(), ctx.paintDpr, faviconForPreview);
        if (!fallbackPreview.isNull()) {
            painter->drawPixmap(ctx.bodyRect.topLeft(), fallbackPreview);
        }
    }
}
