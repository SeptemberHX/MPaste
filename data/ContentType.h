// input: Defines clipboard content types shared across data and UI layers.
// output: Provides a stable enum for item type classification.
// pos: Data-layer shared type definitions.
// update: If I change, update the data/README.md.
#ifndef MPASTE_CONTENTTYPE_H
#define MPASTE_CONTENTTYPE_H

// NOTE: Append new values to preserve on-disk enum compatibility.
enum ContentType {
    All = 0,
    Text,
    Link,
    Image,
    RichText,
    File,
    Color,
    Office
};

#endif // MPASTE_CONTENTTYPE_H
