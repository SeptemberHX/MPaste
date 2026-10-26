// input: Shared card sizing constants for thumbnail generation and UI layouts.
// output: Exposes base card and preview dimensions, and how a picture or text
//         thumbnail takes the preview's shape.
// pos: Data-layer shared metrics for preview sizing.
// update: If I change, update data/README.md.
#ifndef MPASTE_CARDPREVIEWMETRICS_H
#define MPASTE_CARDPREVIEWMETRICS_H

#include <QImage>
#include <QPainter>
#include <QtGlobal>

constexpr int kCardBaseWidth = 256;
constexpr int kCardBaseHeight = 216;
constexpr int kCardHeaderHeight = 56;
constexpr int kCardFooterHeight = 20;
// Stored thumbnails take the shape of the picture area on a card (about 1.42:1),
// so the card shows them whole instead of cropping a second time. Thumbnails
// saved at the earlier 275x224 are reshaped on load (cardPictureThumbnail for
// pictures, cardTextThumbnail for text).
constexpr int kCardPreviewWidth = 256;
constexpr int kCardPreviewHeight = 180;

static_assert(kCardPreviewHeight > 0, "Card preview height must be positive.");

// How far a picture's shape may be from the card's before it is fitted whole
// rather than cut to the card's shape (a phone screenshot, about 3x, still fills).
constexpr qreal kCardPictureCoverLimit = 3.5;

// True when a picture of `size` fills the card cut to its shape; false for a
// long strip or a very tall page, which a cut would turn into a close-up of
// its middle.
inline bool cardPictureCovers(const QSize &size, const QSize &target) {
    if (size.isEmpty() || target.isEmpty()) {
        return true;
    }
    const qreal aspect = size.width() / qreal(size.height());
    const qreal cardAspect = target.width() / qreal(target.height());
    return qMax(aspect / cardAspect, cardAspect / aspect) <= kCardPictureCoverLimit;
}

// A picture as a card thumbnail of `target` pixels: cut to the card's shape
// around its centre, or, past kCardPictureCoverLimit, fitted whole and centred
// on transparency. QImage, so it also runs off the GUI thread.
inline QImage cardPictureThumbnail(const QImage &picture, const QSize &target) {
    if (picture.isNull() || target.isEmpty()) {
        return QImage();
    }
    if (cardPictureCovers(picture.size(), target)) {
        const QImage covered = picture.size() == target ? picture
            : picture.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        return covered.copy((covered.width() - target.width()) / 2, (covered.height() - target.height()) / 2,
                            target.width(), target.height());
    }
    const QImage fitted = picture.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage canvas(target, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    // By pixels: drawn at a point, a high-DPI source (a thumbnail read back from
    // disk) would come out at its logical size, a fraction of the canvas.
    painter.drawImage(QRect(QPoint((target.width() - fitted.width()) / 2, (target.height() - fitted.height()) / 2),
                            fitted.size()), fitted);
    painter.end();
    return canvas;
}

// A text-like thumbnail (text, rich text, code) saved in another shape: its
// content starts at the top, so it is scaled to the width and its top kept.
inline QImage cardTextThumbnail(const QImage &thumbnail, const QSize &target) {
    if (thumbnail.isNull() || target.isEmpty()) {
        return QImage();
    }
    const QImage scaled = thumbnail.scaledToWidth(target.width(), Qt::SmoothTransformation);
    QImage canvas(target, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    // By pixels, as above: a 2x thumbnail drawn at a point filled only the top-left
    // quarter, so the text came out at half size.
    painter.drawImage(QRect(QPoint(0, 0), scaled.size()), scaled);
    painter.end();
    return canvas;
}

#endif // MPASTE_CARDPREVIEWMETRICS_H
