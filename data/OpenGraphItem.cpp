// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// note: image kind
// output: 提供 OpenGraphItem 的实现逻辑。
// pos: data 层中的 OpenGraphItem 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
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

OpenGraphItem::ImageKind OpenGraphItem::getImageKind() const {
    return imageKind;
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

void OpenGraphItem::setImageKind(OpenGraphItem::ImageKind kind) {
    imageKind = kind;
}
