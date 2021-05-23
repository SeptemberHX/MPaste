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
    ClipboardItem(const QPixmap &icon, const QString &text, const QPixmap &image, const QString &html, const QList<QUrl> &urls);

    const bool sameContent(ClipboardItem item) const;

    const bool isEmpty() const;

    ClipboardItem() = default;

    const QString &getText() const;

    const QPixmap &getImage() const;

    const QString &getHtml() const;

    const QList<QUrl> &getUrls() const;

    const QPixmap &getIcon() const;

private:
    QPixmap icon;

    QString text;
    QPixmap image;
    QString html;
    QList<QUrl> urls;
};


#endif //MPASTE_CLIPBOARDITEM_H
