#ifndef MPASTE_CARDBODYRENDERER_H
#define MPASTE_CARDBODYRENDERER_H

#include <QPainter>
#include <QRect>
#include <QModelIndex>
#include "widget/ClipboardCardDelegate.h"

class ClipboardCardDelegate;

struct CardTheme;

// Shared rendering context passed from delegate to body renderers.
struct CardBodyContext {
    const QRect &bodyRect;
    const QRect &previewRect;     // bodyRect with padding
    const CardData &card;
    int scale;
    bool darkTheme;
    int loadingPhase;
    qreal paintDpr;
    QColor bodyTextColor;
    const QModelIndex &index;
    const ClipboardCardDelegate *delegate;
    const CardTheme &theme;
};

// Base class for card body renderers. Each content type has its own subclass.
class CardBodyRenderer {
public:
    virtual ~CardBodyRenderer() = default;
    virtual void paint(QPainter *painter, const CardBodyContext &ctx) const = 0;
};

// Shared utility functions used by multiple renderers.
namespace CardRenderUtils {
    void applyUiFontDefaults(QFont &font);
    void applyMonoFontDefaults(QFont &font);
    void drawElidedText(QPainter *painter, const QRect &rect, const QString &text,
                        const QFont &font, const QColor &color, Qt::Alignment alignment,
                        Qt::TextElideMode elideMode = Qt::ElideRight);
    void drawWrappedText(QPainter *painter, const QRect &rect, const QString &text,
                         const QFont &font, const QColor &color);
    void drawCoverPixmap(QPainter *painter, const QRect &targetRect, const QPixmap &pixmap,
                         const QString &debugName = QString(), const QSize &debugImageSize = QSize());
    void drawContainPixmap(QPainter *painter, const QRect &targetRect, const QPixmap &pixmap);
    void drawManagedVisualPreview(QPainter *painter, const QRect &rect, const CardData &card,
                                  int scale, bool darkTheme, int loadingPhase, bool containMode);
    QString previewTextForCard(const CardData &card);
    void clearCoverPixmapCache();
}

#endif // MPASTE_CARDBODYRENDERER_H
