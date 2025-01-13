//
// Created by ragdoll on 2021/5/24.
//

#include "LocalSaver.h"
#include <QFile>
#include <QDataStream>

bool LocalSaver::saveToFile(const ClipboardItem &item, const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QDataStream out(&file);

    // 保存基本属性
    out << item.getTime() << item.getIcon();
    out << item.getFavicon() << item.getTitle() << item.getUrl();

    const QMimeData* mimeData = item.getMimeData();
    if (mimeData) {
        QStringList formats = mimeData->formats();

        // 直接遍历 formats 列表并写入格式和数据
        for (const QString &format : formats) {
            QByteArray data = mimeData->data(format);
            out << format << data; // 写入格式名称和数据
        }
    }

    file.close();
    return true;
}

ClipboardItem LocalSaver::loadFromFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return ClipboardItem();
    }

    QDataStream in(&file);

    // 读取基本属性
    QDateTime time;
    QPixmap icon;
    QPixmap favicon;
    QString title;
    QString url;
    in >> time >> icon >> favicon >> title >> url;

    // 创建普通指针，让 ClipboardItem 的构造函数接管其所有权
    QMimeData* mimeData = new QMimeData;
    // 循环读取格式和数据，直到文件结束
    while (!in.atEnd()) {
        QString format;
        QByteArray data;

        in >> format >> data;  // 读取格式和数据
        mimeData->setData(format, data);  // 将数据设置到 QMimeData 中
    }

    // ClipboardItem 构造函数会接管 mimeData 的所有权
    ClipboardItem item(icon, mimeData);
    item.setFavicon(favicon);
    item.setTime(time);
    item.setTitle(title);
    item.setUrl(url);

    file.close();
    return item;
}

bool LocalSaver::removeItem(const QString &filePath) {
    QFile file(filePath);
    return file.remove();
}