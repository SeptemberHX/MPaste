// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 ToggleSwitch 的实现逻辑。
// pos: widget 层中的 ToggleSwitch 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#include "ToggleSwitch.h"
#include <QPainter>
#include <QPropertyAnimation>
#include <QMouseEvent>

static const int TRACK_W = 44;
static const int TRACK_H = 22;
static const int HANDLE_D = 14; // knob diameter
static const int HANDLE_MARGIN = 4;

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(TRACK_W, TRACK_H);

    animation_ = new QPropertyAnimation(this, "handlePos", this);
    animation_->setDuration(150);
    animation_->setEasingCurve(QEasingCurve::InOutCubic);

    connect(this, &QAbstractButton::toggled, this, &ToggleSwitch::updateAnimation);
}

QSize ToggleSwitch::sizeHint() const
{
    return {TRACK_W, TRACK_H};
}

void ToggleSwitch::setHandlePos(qreal pos)
{
    handlePos_ = pos;
    update();
}

void ToggleSwitch::updateAnimation()
{
    animation_->stop();
    animation_->setStartValue(handlePos_);
    animation_->setEndValue(isChecked() ? 1.0 : 0.0);
    animation_->start();
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        setChecked(!isChecked());
    }
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Track
    QColor trackColor = isChecked() ? QColor("#0078D4") : QColor("#B0B0B0");
    if (underMouse()) {
        trackColor = isChecked() ? QColor("#006CBC") : QColor("#999999");
    }
    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(rect(), TRACK_H / 2.0, TRACK_H / 2.0);

    // Handle (knob)
    qreal xLeft = HANDLE_MARGIN;
    qreal xRight = TRACK_W - HANDLE_MARGIN - HANDLE_D;
    qreal x = xLeft + handlePos_ * (xRight - xLeft);
    qreal y = (TRACK_H - HANDLE_D) / 2.0;

    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(x, y, HANDLE_D, HANDLE_D));
}
