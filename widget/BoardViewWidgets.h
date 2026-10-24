// input: Depends on Qt Widgets, QPainter, QListView, QElapsedTimer.
// output: Provides board widgets and a rounded hover bar with a locally blurred card backdrop.
// pos: Standalone QWidget subclasses extracted from ScrollItemsWidgetMV.cpp.
#ifndef BOARDVIEWWIDGETS_H
#define BOARDVIEWWIDGETS_H

#include <QColor>
#include <QElapsedTimer>
#include <QListView>
#include <functional>
#include <QPainter>
#include <QPaintEvent>
#include <QWidget>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainterPath>

class HoverActionBar final : public QWidget {
public:
    explicit HoverActionBar(QWidget *parent = nullptr)
        : QWidget(parent) {}

    void refreshBackdrop(QWidget *viewport, const QRect &rect) {
        // Capture only the sibling viewport, never this toolbar or the desktop.
        // Bound refresh work during high-frequency mouse/scroll events.
        if (backdropTimer_.isValid() && backdropTimer_.elapsed() < 60 && rect == backdropRect_) return;
        backdropTimer_.restart();
        backdropRect_ = rect;
        const QPixmap snapshot = viewport->grab(rect);
        if (snapshot.isNull()) return;
        const qreal dpr = snapshot.devicePixelRatioF();
        QImage source = snapshot.toImage();
        source.setDevicePixelRatio(1);
        QGraphicsScene scene;
        auto *item = scene.addPixmap(QPixmap::fromImage(source));
        auto *blur = new QGraphicsBlurEffect;
        blur->setBlurRadius(10 * dpr);
        blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
        item->setGraphicsEffect(blur);
        QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        scene.render(&painter, QRectF(QPointF(), result.size()), QRectF(QPointF(), source.size()));
        painter.end();
        result.setDevicePixelRatio(dpr);
        backdrop_ = QPixmap::fromImage(result);
        update();
    }

    void setCornerRadius(int radius) {
        cornerRadius_ = radius;
        update();
    }

    void setColors(const QColor &background, const QColor &border) {
        background_ = background;
        border_ = border;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        // This is a child widget: Qt propagates the parent/card background.
        // Clearing with Source would punch a rectangular hole through that background.
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF rectF = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath shape;
        shape.addRoundedRect(rectF, cornerRadius_, cornerRadius_);
        painter.setClipPath(shape);
        if (!backdrop_.isNull()) painter.drawPixmap(QPoint(-12, -12), backdrop_);
        painter.fillPath(shape, background_);
        painter.setClipping(false);
        painter.setPen(border_);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape);
    }

private:
    QPixmap backdrop_;
    QElapsedTimer backdropTimer_;
    QRect backdropRect_;
    int cornerRadius_ = 8;
    QColor background_ = QColor(255, 255, 255, 185);
    QColor border_ = QColor(255, 255, 255, 120);
};

class EdgeFadeOverlay final : public QWidget {
public:
    enum Side {
        Left,
        Right
    };

    explicit EdgeFadeOverlay(Side side, QWidget *parent = nullptr)
        : QWidget(parent), side_(side) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void setDark(bool dark) {
        dark_ = dark;
        update();
    }

    void setTransparentInset(int inset) {
        transparentInset_ = qMax(0, inset);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        const QPoint startPoint = side_ == Left ? rect().topLeft() : rect().topRight();
        const QPoint endPoint = side_ == Left ? rect().topRight() : rect().topLeft();
        QLinearGradient gradient(startPoint, endPoint);

        const QColor mistColor = dark_ ? QColor(22, 28, 36) : QColor(232, 236, 239);
        const int denseAlpha = dark_ ? 72 : 192;
        const int softAlpha = dark_ ? 40 : 104;
        const int faintAlpha = dark_ ? 18 : 36;
        QColor dense = mistColor;
        dense.setAlpha(denseAlpha);
        QColor soft = mistColor;
        soft.setAlpha(softAlpha);
        QColor faint = mistColor;
        faint.setAlpha(faintAlpha);
        QColor transparent = mistColor;
        transparent.setAlpha(0);

        const qreal width = qMax<qreal>(1.0, rect().width());
        const qreal insetRatio = qBound(0.0, transparentInset_ / width, 0.85);
        const qreal span = 1.0 - insetRatio;
        gradient.setColorAt(0.00, transparent);
        gradient.setColorAt(insetRatio, dense);
        gradient.setColorAt(insetRatio + span * 0.45, soft);
        gradient.setColorAt(insetRatio + span * 0.78, faint);
        gradient.setColorAt(1.00, transparent);
        painter.fillRect(rect(), gradient);
    }

private:
    Side side_;
    bool dark_ = false;
    int transparentInset_ = 0;
};

class ClipboardBoardView final : public QListView {
public:
    explicit ClipboardBoardView(const QString &debugLabel, QWidget *parent = nullptr)
        : QListView(parent),
          debugLabel_(debugLabel) {}

    void applyViewportMargins(int left, int top, int right, int bottom) {
        setViewportMargins(left, top, right, bottom);
    }

    void refreshItemGeometries() {
        updateGeometries();
    }

    // Painted under the cards on every repaint of the viewport (group dividers).
    void setUnderlayPainter(std::function<void(QPainter &)> painter) { underlayPainter_ = std::move(painter); }

    // Allow the next scrollTo call to go through.
    void setExplicitScrollTo(bool allow) { explicitScrollTo_ = allow; }

    void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible) override {
        // Block auto-scrollTo triggered internally by QListView (e.g.
        // after setCurrentIndex / dataChanged) which yanks the viewport
        // back while the user is browsing.  Only honour calls that our
        // code explicitly requested.
        if (explicitScrollTo_) {
            explicitScrollTo_ = false;
            QListView::scrollTo(index, hint);
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        if (!fpsWindow_.isValid()) {
            resetPaintStats();
            fpsWindow_.start();
        }

        qint64 frameGapMs = 0;
        if (frameGapTimer_.isValid()) {
            frameGapMs = frameGapTimer_.restart();
        } else {
            frameGapTimer_.start();
        }

        QElapsedTimer paintTimer;
        paintTimer.start();
        if (underlayPainter_) {
            QPainter underlay(viewport());
            underlay.setClipRegion(event->region());
            underlayPainter_(underlay);
        }
        QListView::paintEvent(event);
        const qint64 paintMs = paintTimer.elapsed();

        ++frameCount_;
        maxPaintMs_ = qMax(maxPaintMs_, paintMs);
        if (frameGapMs > 0) {
            maxFrameGapMs_ = qMax(maxFrameGapMs_, frameGapMs);
            if (frameGapMs > 20) {
                ++jank20Count_;
            }
            if (frameGapMs > 33) {
                ++jank33Count_;
            }
        }

        if (fpsWindow_.elapsed() >= 1000) {
            qInfo().noquote()
                << QStringLiteral("[board-fps] board=%1 fps=%2 maxFrameGapMs=%3 maxPaintMs=%4 jank>20ms=%5 jank>33ms=%6")
                      .arg(debugLabel_.isEmpty() ? objectName() : debugLabel_)
                      .arg(frameCount_)
                      .arg(maxFrameGapMs_)
                      .arg(maxPaintMs_)
                      .arg(jank20Count_)
                      .arg(jank33Count_);
            resetPaintStats();
            fpsWindow_.restart();
        }
    }

private:
    void resetPaintStats() {
        frameCount_ = 0;
        maxFrameGapMs_ = 0;
        maxPaintMs_ = 0;
        jank20Count_ = 0;
        jank33Count_ = 0;
        frameGapTimer_.invalidate();
    }

    QString debugLabel_;
    QElapsedTimer fpsWindow_;
    QElapsedTimer frameGapTimer_;
    int frameCount_ = 0;
    qint64 maxFrameGapMs_ = 0;
    qint64 maxPaintMs_ = 0;
    int jank20Count_ = 0;
    int jank33Count_ = 0;
    bool explicitScrollTo_ = false;
    std::function<void(QPainter &)> underlayPainter_;
};

#endif // BOARDVIEWWIDGETS_H
