// input: 依赖 Qt Widgets、data 层对象与同层组件声明。
// output: 对外提供 MTextBrowser 的声明接口。
// pos: widget 层中的 MTextBrowser 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
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
