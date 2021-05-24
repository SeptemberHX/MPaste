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
    this->time = QDateTime::currentDateTime();
    this->name = QString::number(this->time.toMSecsSinceEpoch());
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

const QDateTime &ClipboardItem::getTime() const {
    return time;
}

void ClipboardItem::setIcon(const QPixmap &icon) {
    ClipboardItem::icon = icon;
}

void ClipboardItem::setText(const QString &text) {
    ClipboardItem::text = text;
}

void ClipboardItem::setImage(const QPixmap &image) {
    ClipboardItem::image = image;
}

void ClipboardItem::setHtml(const QString &html) {
    ClipboardItem::html = html;
}

void ClipboardItem::setUrls(const QList<QUrl> &urls) {
    ClipboardItem::urls = urls;
}

void ClipboardItem::setTime(const QDateTime &time) {
    ClipboardItem::time = time;
}

const QString &ClipboardItem::getName() const {
    return name;
}

ClipboardItem::ClipboardItem() {
    this->name = QString::number(QDateTime::currentMSecsSinceEpoch());
}

void ClipboardItem::setName(const QString &name) {
    ClipboardItem::name = name;
}
