//
// Created by ragdoll on 2021/5/22.
//

#include "ClipboardItem.h"


ClipboardItem::ClipboardItem(const QPixmap &icon, const QString &text, const QPixmap &image, const QString &html, const QList<QUrl> &urls)
    : icon(icon)
    , text(text)
    , image(image)
    , html(html)
    , urls(urls)
{

}

const QString &ClipboardItem::getText() const {
    return text;
}

const QPixmap &ClipboardItem::getImage() const {
    return image;
}

const QString &ClipboardItem::getHtml() const {
    return html;
}

const QList<QUrl> &ClipboardItem::getUrls() const {
    return urls;
}

const QPixmap &ClipboardItem::getIcon() const {
    return icon;
}

const bool ClipboardItem::sameContent(ClipboardItem item) const {
    if (this->getHtml() != item.getHtml()) return false;
    if (this->getText() != item.getText()) return false;
    if (this->getImage().toImage() != item.getImage().toImage()) return false;
    return true;
}

const bool ClipboardItem::isEmpty() const {
    return this->getHtml().isEmpty() && this->getText().isEmpty() && this->getImage().isNull() && this->getUrls().isEmpty();
}
