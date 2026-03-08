// input: 依赖对应头文件、Qt 运行时与资源/服务组件。
// output: 对外提供 MTextBrowser 的实现行为。
// pos: widget 层中的 MTextBrowser 实现文件。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
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
