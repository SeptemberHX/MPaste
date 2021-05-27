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
    ClipboardItem(const QPixmap &icon, const QString &text, const QPixmap &image, const QString &html, const QList<QUrl> &urls, const QColor &color);

    const bool sameContent(ClipboardItem item) const;

    const bool isEmpty() const;

    const bool contains(const QString &keyword) const;

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

    void setName(const QString &name);

    const QColor &getColor() const;

    void setColor(const QColor &color);

    const QString &getTitle() const;

    void setTitle(const QString &title);

    const QString &getUrl() const;

    void setUrl(const QString &url);

    const QPixmap &getFavicon() const;

    void setFavicon(const QPixmap &favicon);

private:
    QPixmap icon;

    QString text;
    QPixmap image;
    QString html;
    QList<QUrl> urls;
    QDateTime time;
    QColor color;

    QString name;

    QString title;
    QString url;
    QPixmap favicon;
};


#endif //MPASTE_CLIPBOARDITEM_H
