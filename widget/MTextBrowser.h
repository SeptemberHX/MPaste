// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 MTextBrowser 的声明接口。
// pos: widget 层中的 MTextBrowser 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
//
// Created by ragdoll on 2021/5/23.
//

#ifndef MPASTE_MTEXTBROWSER_H
#define MPASTE_MTEXTBROWSER_H

#include <QTextBrowser>

class MTextBrowser : public QTextBrowser {

public:
    explicit MTextBrowser(QWidget *parent = nullptr);

private:
    void wheelEvent(QWheelEvent *e) override;
};


#endif //MPASTE_MTEXTBROWSER_H
