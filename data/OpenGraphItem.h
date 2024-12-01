#ifndef MPASTE_OPENGRAPHITEM_H
#define MPASTE_OPENGRAPHITEM_H

#include <QString>
#include <QPixmap>
#include <QMetaType>

class OpenGraphItem {
public:
    OpenGraphItem(const QString &title, const QString &description, const QPixmap &image);

    OpenGraphItem() = default;
    ~OpenGraphItem() = default;

    // Make the class movable and copyable
    OpenGraphItem(const OpenGraphItem&) = default;
    OpenGraphItem& operator=(const OpenGraphItem&) = default;
    OpenGraphItem(OpenGraphItem&&) = default;
    OpenGraphItem& operator=(OpenGraphItem&&) = default;

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