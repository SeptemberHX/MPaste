// input: Shared card sizing constants for thumbnail generation and UI layouts.
// output: Exposes base card and preview dimensions for consistent rendering.
// pos: Data-layer shared metrics for preview sizing.
// update: If I change, update data/README.md.
#ifndef MPASTE_CARDPREVIEWMETRICS_H
#define MPASTE_CARDPREVIEWMETRICS_H

#include <QtGlobal>

constexpr int kCardBaseWidth = 256;
constexpr int kCardBaseHeight = 216;
constexpr int kCardHeaderHeight = 56;
constexpr int kCardFooterHeight = 20;
// Stored thumbnails keep their original 275x224 size: loaded thumbnails are
// rescaled to this size ignoring aspect, so it must not follow the card size.
constexpr int kCardPreviewWidth = 275;
constexpr int kCardPreviewHeight = 224;

static_assert(kCardPreviewHeight > 0, "Card preview height must be positive.");

#endif // MPASTE_CARDPREVIEWMETRICS_H
