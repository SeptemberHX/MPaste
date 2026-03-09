// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 ToggleSwitch 的声明接口。
// pos: widget 层中的 ToggleSwitch 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#ifndef TOGGLESWITCH_H
#define TOGGLESWITCH_H

#include <QAbstractButton>
#include <QPropertyAnimation>

class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal handlePos READ handlePos WRITE setHandlePos)

public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateAnimation();
    qreal handlePos() const { return handlePos_; }
    void setHandlePos(qreal pos);

    qreal handlePos_ = 0.0;
    QPropertyAnimation *animation_;
};

#endif // TOGGLESWITCH_H
