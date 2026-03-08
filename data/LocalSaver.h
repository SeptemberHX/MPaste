// input: 依赖 Qt 数据类型、mime/图像/时间对象与上层调用方。
// output: 对外提供 LocalSaver 的数据声明。
// pos: data 层中的 LocalSaver 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
//
// Created by ragdoll on 2021/5/24.
//

#ifndef MPASTE_LOCALSAVER_H
#define MPASTE_LOCALSAVER_H

#include <QString>
#include "ClipboardItem.h"

class LocalSaver {

public:
    bool saveToFile(const ClipboardItem &item, const QString &filePath);
    bool removeItem(const QString &filePath);
    ClipboardItem loadFromFile(const QString &filePath);
};


#endif //MPASTE_LOCALSAVER_H
