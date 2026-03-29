// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 AboutWidget 的实现逻辑。
// pos: widget 层中的 AboutWidget 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#include "AboutWidget.h"
#include "ui_AboutWidget.h"

AboutWidget::AboutWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AboutWidget)
{
    ui->setupUi(this);
#ifdef MPASTE_VERSION
    ui->label_2->setText(QStringLiteral("MPaste V%1").arg(QStringLiteral(MPASTE_VERSION)));
#endif
}

AboutWidget::~AboutWidget()
{
    delete ui;
}
