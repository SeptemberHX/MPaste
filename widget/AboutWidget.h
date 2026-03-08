// input: 依赖 Qt Widgets、data 层对象与同层组件声明。
// output: 对外提供 AboutWidget 的声明接口。
// pos: widget 层中的 AboutWidget 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
#ifndef ABOUTWIDGET_H
#define ABOUTWIDGET_H

#include <QWidget>

namespace Ui {
class AboutWidget;
}

class AboutWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AboutWidget(QWidget *parent = nullptr);
    ~AboutWidget();

private:
    Ui::AboutWidget *ui;
};

#endif // ABOUTWIDGET_H
