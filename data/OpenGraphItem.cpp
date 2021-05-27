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
