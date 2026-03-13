// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// note: image kind
// output: 对外提供 OpenGraphItem 的声明接口。
// pos: data 层中的 OpenGraphItem 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#ifndef MPASTE_OPENGRAPHITEM_H
#define MPASTE_OPENGRAPHITEM_H

#include <QString>
#include <QPixmap>
#include <QMetaType>

class OpenGraphItem {
public:
    enum ImageKind {
        ImageUnknown = 0,
        PreviewImage,
        IconImage
    };

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
    ImageKind getImageKind() const;

    void setTitle(const QString &title);
    void setDescription(const QString &description);
    void setImage(const QPixmap &image);
    void setImageKind(ImageKind kind);

private:
    QString title;
    QString description;
    QPixmap image;
    ImageKind imageKind = ImageUnknown;
};

Q_DECLARE_METATYPE(OpenGraphItem)

#endif //MPASTE_OPENGRAPHITEM_H
