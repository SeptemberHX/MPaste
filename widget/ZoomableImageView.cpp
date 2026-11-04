// input: Depends on ZoomableImageView.h and Qt Widgets.
// output: Implements the fitted, zoomable and draggable picture view.
// pos: Widget-layer view shared by the Space preview and the details dialog.
// update: If I change, update this header block and my folder README.md.
#include "ZoomableImageView.h"

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>

namespace {
constexpr qreal kMinScale = 0.05;   // of the picture's own size
constexpr qreal kMaxScale = 8.0;
constexpr qreal kWheelStep = 1.15;
}

class ZoomableImageCanvas : public QWidget {
public:
    using QWidget::QWidget;

    void setPixmap(const QPixmap &pixmap) {
        pixmap_ = pixmap;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        if (pixmap_.isNull() || width() <= 0) {
            return;
        }
        QPainter painter(this);
        painter.setClipRegion(event->region());
        // Smooth until each picture pixel covers several screen pixels; past
        // that, sharp squares show the pixels, as an image viewer does.
        const qreal devicePixelsPerPixel = width() * devicePixelRatioF() / pixmap_.width();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, devicePixelsPerPixel < 3.0);
        painter.drawPixmap(rect(), pixmap_);
    }

private:
    QPixmap pixmap_;
};

ZoomableImageView::ZoomableImageView(QWidget *parent)
    : QScrollArea(parent) {
    canvas_ = new ZoomableImageCanvas(this);
    canvas_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    canvas_->setFocusPolicy(Qt::NoFocus);
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(false);
    setAlignment(Qt::AlignCenter);
    setFocusPolicy(Qt::NoFocus);
    setWidget(canvas_);
    setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    viewport()->setAutoFillBackground(false);
    viewport()->setCursor(Qt::OpenHandCursor);
    viewport()->installEventFilter(this);
}

void ZoomableImageView::setImage(const QImage &image) {
    setPixmap(image.isNull() ? QPixmap() : QPixmap::fromImage(image));
}

void ZoomableImageView::setPixmap(const QPixmap &pixmap) {
    // One full-size copy, the canvas's; the view keeps only its size.
    QPixmap shown = pixmap;
    if (!shown.isNull()) {
        shown.setDevicePixelRatio(1.0);
    }
    imageSize_ = shown.size();
    canvas_->setPixmap(shown);
    zoom_ = 1.0;
    relayout();
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
}

void ZoomableImageView::clear() {
    setPixmap(QPixmap());
}

void ZoomableImageView::setFitWidth(bool fitWidth) {
    if (fitWidth_ != fitWidth) {
        fitWidth_ = fitWidth;
        relayout();
    }
}

void ZoomableImageView::resetZoom() {
    zoom_ = 1.0;
    relayout();
}

void ZoomableImageView::relayout() {
    if (imageSize_.isEmpty()) {
        canvas_->resize(1, 1);
        return;
    }
    const QSize view = viewport()->size();
    const QSize size = imageSize_;
    qreal fit = 1.0;
    if (view.isValid() && size.isValid()) {
        const qreal byWidth = view.width() / qreal(size.width());
        const qreal byHeight = view.height() / qreal(size.height());
        fit = fitWidth_ ? byWidth : qMin(byWidth, byHeight);
    }
    // Fitted is always allowed, however small: a very long picture fits
    // below the usual floor, and zooming from there must still move.
    fit = qMax(fit, 1e-4);
    const qreal scale = qBound(qMin(kMinScale, fit), fit * zoom_, qMax(kMaxScale, fit));
    zoom_ = scale / fit;
    canvas_->resize((QSizeF(size) * scale).toSize().expandedTo(QSize(1, 1)));
    canvas_->update();
}

void ZoomableImageView::zoomBy(qreal factor, QPoint anchor) {
    if (imageSize_.isEmpty()) {
        return;
    }
    if (anchor.x() < 0 || anchor.y() < 0) {
        anchor = viewport()->rect().center();
    }
    // Where on the picture the anchor is, as a fraction of its size.
    const QSize before = canvas_->size();
    const QPointF onCanvas = canvas_->mapFrom(viewport(), anchor);
    const QPointF fraction(qBound(0.0, onCanvas.x() / qMax(1, before.width()), 1.0),
                           qBound(0.0, onCanvas.y() / qMax(1, before.height()), 1.0));
    zoom_ *= factor;
    relayout();
    // Scroll that point back under the anchor.
    const QSize after = canvas_->size();
    horizontalScrollBar()->setValue(qRound(fraction.x() * after.width() - anchor.x()));
    verticalScrollBar()->setValue(qRound(fraction.y() * after.height() - anchor.y()));
}

bool ZoomableImageView::eventFilter(QObject *watched, QEvent *event) {
    if (watched != viewport()) {
        return QScrollArea::eventFilter(watched, event);
    }
    switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && hasImage()) {
                dragging_ = true;
                dragStart_ = mouse->globalPosition().toPoint();
                dragStartScroll_ = QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
                viewport()->setCursor(Qt::ClosedHandCursor);
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (dragging_) {
                const QPoint delta = mouse->globalPosition().toPoint() - dragStart_;
                horizontalScrollBar()->setValue(dragStartScroll_.x() - delta.x());
                verticalScrollBar()->setValue(dragStartScroll_.y() - delta.y());
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && dragging_) {
                dragging_ = false;
                viewport()->setCursor(Qt::OpenHandCursor);
                return true;
            }
            break;
        }
        case QEvent::MouseButtonDblClick:
            if (hasImage()) {
                resetZoom();
                return true;
            }
            break;
        case QEvent::Wheel: {
            auto *wheel = static_cast<QWheelEvent *>(event);
            // A mouse wheel zooms, as in an image viewer; a touchpad's
            // two-finger scroll (which comes in phases) moves the picture,
            // and with Ctrl, as Windows sends a pinch, zooms too.
            const bool fromWheel = wheel->phase() == Qt::NoScrollPhase;
            if (hasImage() && (fromWheel || (wheel->modifiers() & Qt::ControlModifier))) {
                const QPoint angle = wheel->angleDelta();
                const qreal steps = (angle.y() != 0 ? angle.y() : angle.x()) / 120.0;
                if (!qFuzzyIsNull(steps)) {
                    zoomBy(std::pow(kWheelStep, steps), wheel->position().toPoint());
                }
                return true;
            }
            break;
        }
        case QEvent::NativeGesture: {
            // A touchpad pinch, where the system reports one (GNOME, macOS).
            auto *gesture = static_cast<QNativeGestureEvent *>(event);
            if (hasImage() && gesture->gestureType() == Qt::ZoomNativeGesture) {
                zoomBy(1.0 + gesture->value(), gesture->position().toPoint());
                return true;
            }
            if (hasImage() && gesture->gestureType() == Qt::SmartZoomNativeGesture) {
                resetZoom();
                return true;
            }
            break;
        }
        case QEvent::Resize:
            relayout();
            break;
        default:
            break;
    }
    return QScrollArea::eventFilter(watched, event);
}
