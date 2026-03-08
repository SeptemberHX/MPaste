// input: 依赖对应头文件、Qt 运行时与资源/服务组件。
// output: 对外提供 AboutWidget 的实现行为。
// pos: widget 层中的 AboutWidget 实现文件。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
#include "AboutWidget.h"
#include "ui_AboutWidget.h"

AboutWidget::AboutWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AboutWidget)
{
    ui->setupUi(this);
}

AboutWidget::~AboutWidget()
{
    delete ui;
}
