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
// Stored thumbnails take the shape of the picture area on a card (about 1.42:1),
// so the card shows them whole instead of cropping a second time. Thumbnails
// saved at the earlier 275x224 are cover-cropped to this on load.
constexpr int kCardPreviewWidth = 256;
constexpr int kCardPreviewHeight = 180;

static_assert(kCardPreviewHeight > 0, "Card preview height must be positive.");

#endif // MPASTE_CARDPREVIEWMETRICS_H
