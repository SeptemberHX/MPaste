// GlassPagePopup.h — Glass dropdown popup for the page selector.
// Extracted from MPasteWidget.cpp for use across split implementation files.
#ifndef GLASSPAGEPOPUP_H
#define GLASSPAGEPOPUP_H

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QCursor>

#include "WindowBlurHelper.h"

class GlassPagePopup final : public QWidget {
    Q_OBJECT
public:
    explicit GlassPagePopup(QWidget *parent = nullptr)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
    }

    void setPages(int totalPages, int currentPage) {
        totalPages_ = qMax(1, totalPages);
        currentPage_ = qBound(1, currentPage, totalPages_);
        hoveredPage_ = -1;
        scrollY_ = 0;
        recalcSize();
        ensurePageVisible(currentPage_);
    }

    void popup(const QPoint &globalPos) {
        move(globalPos);
        show();
        WindowBlurHelper::enableBlurBehind(this, dark_);
        raise();
    }

    void setDark(bool dark) { dark_ = dark; }

signals:
    void pageSelected(int page);

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        const QColor bg = dark_ ? QColor(30, 36, 48, 1) : QColor(245, 245, 248, 1);
        const QColor border = dark_ ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 22);
        p.setPen(QPen(border, 1));
        p.setBrush(bg);
        p.drawRoundedRect(r, 8, 8);

        const QColor textColor = dark_ ? QColor(230, 237, 245) : QColor(30, 41, 54);
        const QColor hoverBg = dark_ ? QColor(255, 255, 255, 24) : QColor(0, 0, 0, 14);
        const QColor currentBg = QColor(74, 144, 226, dark_ ? 90 : 60);

        QFont font;
        font.setPixelSize(13);
        font.setWeight(QFont::DemiBold);
        p.setFont(font);

        p.setClipRect(QRect(kPad, kPad, width() - kPad * 2, height() - kPad * 2));

        const int cols = columns();
        for (int i = 0; i < totalPages_; ++i) {
            const int page = i + 1;
            const QRect cell = cellRect(i, cols);
            if (cell.bottom() < kPad || cell.top() > height() - kPad) {
                continue;
            }
            if (page == currentPage_) {
                p.setPen(Qt::NoPen);
                p.setBrush(currentBg);
                p.drawRoundedRect(cell.adjusted(1, 1, -1, -1), 6, 6);
            } else if (page == hoveredPage_) {
                p.setPen(Qt::NoPen);
                p.setBrush(hoverBg);
                p.drawRoundedRect(cell.adjusted(1, 1, -1, -1), 6, 6);
            }
            p.setPen(textColor);
            p.drawText(cell, Qt::AlignCenter, QString::number(page));
        }

        if (needsScroll()) {
            p.setClipping(false);
            const int totalContentH = totalRows() * kCellH;
            const int viewH = visibleHeight();
            const int trackH = viewH - 4;
            const int thumbH = qMax(12, trackH * viewH / totalContentH);
            const int thumbY = kPad + 2 + (trackH - thumbH) * scrollY_ / maxScroll();
            const int trackX = width() - kPad;
            p.setPen(Qt::NoPen);
            p.setBrush(dark_ ? QColor(255, 255, 255, 36) : QColor(0, 0, 0, 28));
            p.drawRoundedRect(QRectF(trackX, thumbY, 3, thumbH), 1.5, 1.5);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        const int page = pageAtPos(event->pos());
        if (page != hoveredPage_) {
            hoveredPage_ = page;
            update();
        }
    }

    void mousePressEvent(QMouseEvent *event) override {
        const int page = pageAtPos(event->pos());
        if (page > 0) {
            emit pageSelected(page);
        }
        close();
    }

    void wheelEvent(QWheelEvent *event) override {
        if (!needsScroll()) return;
        const int delta = event->angleDelta().y();
        scrollY_ = qBound(0, scrollY_ - (delta > 0 ? kCellH : -kCellH), maxScroll());
        hoveredPage_ = pageAtPos(mapFromGlobal(QCursor::pos()));
        update();
        event->accept();
    }

    void leaveEvent(QEvent *) override {
        hoveredPage_ = -1;
        update();
    }

private:
    static constexpr int kCellW = 38;
    static constexpr int kCellH = 30;
    static constexpr int kPad = 6;
    static constexpr int kMaxVisibleRows = 4;

    int columns() const {
        if (totalPages_ <= 5) return qMax(1, totalPages_);
        if (totalPages_ <= 20) return 5;
        return 6;
    }

    int totalRows() const {
        const int cols = columns();
        return (totalPages_ + cols - 1) / cols;
    }

    int visibleHeight() const {
        return qMin(totalRows(), kMaxVisibleRows) * kCellH;
    }

    bool needsScroll() const { return totalRows() > kMaxVisibleRows; }

    int maxScroll() const {
        return qMax(0, totalRows() * kCellH - visibleHeight());
    }

    void recalcSize() {
        const int cols = columns();
        const int scrollbarW = needsScroll() ? 5 : 0;
        resize(cols * kCellW + kPad * 2 + scrollbarW,
               visibleHeight() + kPad * 2);
    }

    void ensurePageVisible(int page) {
        if (!needsScroll()) return;
        const int cols = columns();
        const int row = (page - 1) / cols;
        const int rowTop = row * kCellH;
        const int rowBot = rowTop + kCellH;
        const int viewH = visibleHeight();
        if (rowTop < scrollY_) {
            scrollY_ = rowTop;
        } else if (rowBot > scrollY_ + viewH) {
            scrollY_ = rowBot - viewH;
        }
        scrollY_ = qBound(0, scrollY_, maxScroll());
    }

    QRect cellRect(int index, int cols) const {
        const int col = index % cols;
        const int row = index / cols;
        return QRect(kPad + col * kCellW,
                     kPad + row * kCellH - scrollY_,
                     kCellW, kCellH);
    }

    int pageAtPos(const QPoint &pos) const {
        const int cols = columns();
        const int col = (pos.x() - kPad) / kCellW;
        const int row = (pos.y() - kPad + scrollY_) / kCellH;
        if (col < 0 || col >= cols || row < 0
            || pos.x() < kPad || pos.y() < kPad
            || pos.x() > width() - kPad || pos.y() > height() - kPad) {
            return -1;
        }
        const int index = row * cols + col;
        return (index >= 0 && index < totalPages_) ? index + 1 : -1;
    }

    int totalPages_ = 1;
    int currentPage_ = 1;
    int hoveredPage_ = -1;
    int scrollY_ = 0;
    bool dark_ = false;
};

#endif // GLASSPAGEPOPUP_H
