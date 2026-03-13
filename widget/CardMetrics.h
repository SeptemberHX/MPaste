// input: Shared card sizing helpers for delegate-based layouts.
// output: Exposes stable card dimensions used by list views and delegates.
// pos: Widget-layer shared metrics for clipboard cards.
#ifndef MPASTE_CARDMETRICS_H
#define MPASTE_CARDMETRICS_H

#include <QSize>
#include <QtGlobal>

constexpr int kCardBaseWidth = 275;
constexpr int kCardBaseHeight = 300;
constexpr int kShadowRightPadding = 10;
constexpr int kShadowBottomPadding = 12;

inline QSize cardOuterSizeForScale(int scale) {
    return QSize(kCardBaseWidth * scale / 100 + qMax(4, kShadowRightPadding * scale / 100),
                 kCardBaseHeight * scale / 100 + qMax(6, kShadowBottomPadding * scale / 100));
}

#endif // MPASTE_CARDMETRICS_H
