//
// Created by ragdoll on 2021/5/22.
//

#include "ClipboardItem.h"


ClipboardItem::ClipboardItem(const QString &text, const QPixmap &image, const QString &html, const QList<QUrl> &urls)
    : text(text)
    , image(image)
    , html(html)
    , urls(urls)
{

}
