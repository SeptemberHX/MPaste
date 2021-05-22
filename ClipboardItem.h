//
// Created by ragdoll on 2021/5/22.
//

#ifndef MPASTE_CLIPBOARDITEM_H
#define MPASTE_CLIPBOARDITEM_H

#include <QMimeData>
#include <QPixmap>
#include <QList>
#include <QUrl>

class ClipboardItem {

public:
    ClipboardItem(const QString &text, const QPixmap &image, const QString &html, const QList<QUrl> &urls);

private:
    QString text;
    QPixmap image;
    QString html;
    QList<QUrl> urls;
};


#endif //MPASTE_CLIPBOARDITEM_H
