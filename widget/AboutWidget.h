// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 AboutWidget 的声明接口。
// pos: widget 层中的 AboutWidget 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#ifndef ABOUTWIDGET_H
#define ABOUTWIDGET_H

#include <QDialog>
#include <QPoint>

namespace Ui {
class AboutWidget;
}

class AboutWidget : public QDialog
{
    Q_OBJECT

public:
    explicit AboutWidget(QWidget *parent = nullptr);
    ~AboutWidget();
    void applyTheme(bool dark);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    Ui::AboutWidget *ui;
    QPoint dragPos_;
    bool darkTheme_ = false;
};

#endif // ABOUTWIDGET_H
