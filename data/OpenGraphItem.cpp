// input: 依赖对应头文件、Qt 序列化/网络/图像能力。
// output: 对外提供 OpenGraphItem 的数据实现。
// pos: data 层中的 OpenGraphItem 实现文件。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
//
// Created by ragdoll on 2021/5/27.
//

#include "OpenGraphItem.h"

OpenGraphItem::OpenGraphItem(const QString &title, const QString &description, const QPixmap &image)
    : title(title)
    , description(description)
    , image(image)
{

}

const QString &OpenGraphItem::getTitle() const {
    return title;
}

const QString &OpenGraphItem::getDescription() const {
    return description;
}

const QPixmap &OpenGraphItem::getImage() const {
    return image;
}

void OpenGraphItem::setTitle(const QString &title) {
    OpenGraphItem::title = title;
}

void OpenGraphItem::setDescription(const QString &description) {
    OpenGraphItem::description = description;
}

void OpenGraphItem::setImage(const QPixmap &image) {
    OpenGraphItem::image = image;
}
