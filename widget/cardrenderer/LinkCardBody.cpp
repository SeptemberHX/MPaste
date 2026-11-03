#include "LinkCardBody.h"

#include <QUrl>

bool LinkCardBody::showsPagePicture(const CardData &card) {
    // Its page's picture is saved: ready, or still being read from disk (the
    // board lets go of pictures it does not show and reads them back).
    return card.previewState == ClipboardBoardModel::PreviewReady
        || card.previewState == ClipboardBoardModel::PreviewLoading;
}

void LinkCardBody::paint(QPainter *painter, const CardBodyContext &ctx) const {
    QUrl currentUrl;
    if (!ctx.card.url.isEmpty()) {
        currentUrl = QUrl(ctx.card.url);
    } else if (!ctx.card.normalizedUrls.isEmpty()) {
        currentUrl = ctx.card.normalizedUrls.first();
    } else {
        currentUrl = QUrl(ctx.card.normalizedText.left(512).trimmed());
    }

    if (showsPagePicture(ctx.card)) {
        CardRenderUtils::drawManagedVisualPreview(painter, ctx.bodyRect, ctx.card, ctx.scale, ctx.darkTheme,
                                                  ctx.loadingPhase, false);
        return;
    }
    CardRenderUtils::paintWebAddress(painter, ctx, currentUrl, ctx.card.title, ctx.card.favicon);
}
