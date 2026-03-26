// input: Shared preview-mode enum used across data, service, and widget layers.
// output: Provides a stable preview kind classification for cards and previews.
// pos: Data-layer shared preview-kind definitions.
// update: If I change, update data/README.md.
#ifndef MPASTE_PREVIEWKIND_H
#define MPASTE_PREVIEWKIND_H

enum ClipboardPreviewKind {
    TextPreview = 0,
    VisualPreview
};

#endif // MPASTE_PREVIEWKIND_H
