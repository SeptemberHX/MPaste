// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 MTextBrowser 的实现逻辑。
// pos: widget 层中的 MTextBrowser 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
//
// Created by ragdoll on 2021/5/23.
//

#include "MTextBrowser.h"
#include <QWheelEvent>

void MTextBrowser::wheelEvent(QWheelEvent *e) {
    e->ignore();
}

MTextBrowser::MTextBrowser(QWidget *parent) : QTextBrowser(parent) {

}
