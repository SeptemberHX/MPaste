//
// Created by ragdoll on 2021/5/27.
//

#ifndef MPASTE_OPENGRAPHITEM_H
#define MPASTE_OPENGRAPHITEM_H

#include <QString>
#include <QPixmap>

class OpenGraphItem {

public:
    OpenGraphItem(const QString &title, const QString &description, const QPixmap &image);

    OpenGraphItem() = default;

    const QString &getTitle() const;

    const QString &getDescription() const;

    const QPixmap &getImage() const;

    void setTitle(const QString &title);

    void setDescription(const QString &description);

    void setImage(const QPixmap &image);

private:
    QString title;
    QString description;
    QPixmap image;
};

Q_DECLARE_METATYPE(OpenGraphItem)

#endif //MPASTE_OPENGRAPHITEM_H
