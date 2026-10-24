// input: Shared card sizing helpers for delegate-based layouts.
// output: Exposes stable card dimensions used by list views and delegates.
// pos: Widget-layer shared metrics for clipboard cards.
#ifndef MPASTE_CARDMETRICS_H
#define MPASTE_CARDMETRICS_H

#include <QPoint>
#include <QSize>
#include <QtGlobal>

#include "data/CardPreviewMetrics.h"

constexpr int kShadowRightPadding = 10;
// The shadow reaches 6px below the card; the selection ring 5px.
constexpr int kShadowBottomPadding = 7;

// Room around the card for the selection ring: a 3px gap plus a 2px ring.
inline int cardSelectionMarginForScale(int scale) {
    return qMax(5, 5 * scale / 100);
}

// Where the card itself starts inside its item rect.
inline QPoint cardOriginForScale(int scale) {
    const int margin = cardSelectionMarginForScale(scale);
    return QPoint(margin, margin);
}

inline QSize cardOuterSizeForScale(int scale) {
    const int margin = cardSelectionMarginForScale(scale);
    return QSize(margin + kCardBaseWidth * scale / 100 + qMax(margin, kShadowRightPadding * scale / 100),
                 margin + kCardBaseHeight * scale / 100 + qMax(margin, kShadowBottomPadding * scale / 100));
}

// Neighbouring items overlap by the left margin so the visible gap between cards stays the same.
inline int cardGridWidthForScale(int scale, int spacing) {
    return cardOuterSizeForScale(scale).width() + spacing - cardSelectionMarginForScale(scale);
}

#endif // MPASTE_CARDMETRICS_H
