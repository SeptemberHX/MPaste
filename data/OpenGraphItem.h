// input: 依赖 Qt 数据类型、mime/图像/时间对象与上层调用方。
// output: 对外提供 OpenGraphItem 的数据声明。
// pos: data 层中的 OpenGraphItem 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
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