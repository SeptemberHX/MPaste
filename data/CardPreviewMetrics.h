// input: Shared card sizing constants for thumbnail generation and UI layouts.
// output: Exposes base card and preview dimensions for consistent rendering.
// pos: Data-layer shared metrics for preview sizing.
// update: If I change, update data/README.md.
#ifndef MPASTE_CARDPREVIEWMETRICS_H
#define MPASTE_CARDPREVIEWMETRICS_H

#include <QtGlobal>

constexpr int kCardBaseWidth = 275;
constexpr int kCardBaseHeight = 300;
constexpr int kCardHeaderHeight = 56;
constexpr int kCardFooterHeight = 20;
constexpr int kCardPreviewWidth = kCardBaseWidth;
constexpr int kCardPreviewHeight = kCardBaseHeight - kCardHeaderHeight - kCardFooterHeight;

static_assert(kCardPreviewHeight > 0, "Card preview height must be positive.");

#endif // MPASTE_CARDPREVIEWMETRICS_H
