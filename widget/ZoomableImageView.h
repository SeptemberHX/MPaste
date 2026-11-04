// input: Depends on Qt Widgets.
// output: Exposes a scrollable picture view that fits a picture to its window and zooms and pans it.
// pos: Widget-layer view shared by the Space preview and the details dialog.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_ZOOMABLEIMAGEVIEW_H
#define MPASTE_ZOOMABLEIMAGEVIEW_H

#include <QImage>
#include <QScrollArea>

class ZoomableImageCanvas;

// A picture fitted to the view, zoomed with the mouse wheel, a touchpad pinch
// or Ctrl+wheel around the pointer, moved by dragging or a two-finger scroll.
// Only the part on screen is drawn, so zooming far into a long screenshot
// costs no more than the view it shows in.
class ZoomableImageView : public QScrollArea {
    Q_OBJECT

public:
    explicit ZoomableImageView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setPixmap(const QPixmap &pixmap);
    void clear();
    bool hasImage() const { return !imageSize_.isEmpty(); }
    QSize imageSize() const { return imageSize_; }

    // Zooms by factor, keeping the picture point under anchor (viewport
    // coordinates; the centre when none) where it is.
    void zoomBy(qreal factor, QPoint anchor = QPoint(-1, -1));
    // Back to fitted to the view.
    void resetZoom();
    // How the picture is shown when fitted: whole, or as wide as the view
    // (a long screenshot then reads from its top).
    void setFitWidth(bool fitWidth);
    // Room left clear around the picture when it is fitted.
    void setInset(int inset) { setViewportMargins(inset, inset, inset, inset); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void relayout();

    ZoomableImageCanvas *canvas_ = nullptr;
    QSize imageSize_;
    qreal zoom_ = 1.0;   // relative to fitted
    bool fitWidth_ = false;
    bool dragging_ = false;
    QPoint dragStart_;
    QPoint dragStartScroll_;
};

#endif // MPASTE_ZOOMABLEIMAGEVIEW_H
