//
// Created by ragdoll on 2021/5/22.
//

#ifndef MPASTE_CLIPBOARDITEM_H
#define MPASTE_CLIPBOARDITEM_H

#include <QMimeData>
#include <QPixmap>
#include <QList>
#include <QUrl>
#include <QDateTime>

class ClipboardItem {

public:
    ClipboardItem(const QPixmap &icon, const QString &text, const QPixmap &image, const QString &html, const QList<QUrl> &urls);

    const bool sameContent(ClipboardItem item) const;

    const bool isEmpty() const;

    ClipboardItem();

    const QString &getText() const;

    const QPixmap &getImage() const;

    const QString &getHtml() const;

    const QList<QUrl> &getUrls() const;

    const QPixmap &getIcon() const;

    const QDateTime &getTime() const;

    void setIcon(const QPixmap &icon);

    void setText(const QString &text);

    void setImage(const QPixmap &image);

    void setHtml(const QString &html);

    void setUrls(const QList<QUrl> &urls);

    void setTime(const QDateTime &time);

    const QString &getName() const;

private:
    QPixmap icon;

    QString text;
    QPixmap image;
    QString html;
    QList<QUrl> urls;
    QDateTime time;

    QString name;
};


#endif //MPASTE_CLIPBOARDITEM_H
