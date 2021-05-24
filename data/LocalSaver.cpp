//
// Created by ragdoll on 2021/5/24.
//

#include "LocalSaver.h"
#include <QFile>
#include <QDataStream>

bool LocalSaver::saveToFile(const ClipboardItem &item, const QString &filePath) {
    QFile file(filePath);
    file.open(QIODevice::WriteOnly);
    QDataStream out(&file);
    out << item.getTime() << item.getUrls() << item.getImage() << item.getText() << item.getHtml() << item.getIcon() << item.getName() << item.getColor();
    file.close();
    return true;
}

ClipboardItem LocalSaver::loadFromFile(const QString &filePath) {
    ClipboardItem item;
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);
    QDataStream in(&file);

    QDateTime time;
    in >> time;
    item.setTime(time);

    QList<QUrl> urls;
    in >> urls;
    item.setUrls(urls);

    QPixmap image;
    in >> image;
    item.setImage(image);

    QString text;
    in >> text;
    item.setText(text);

    QString html;
    in >> html;
    item.setHtml(html);

    QPixmap icon;
    in >> icon;
    item.setIcon(icon);

    QString name;
    in >> name;
    item.setName(name);

    QColor color;
    in >> color;
    item.setColor(color);

    return item;
}

bool LocalSaver::removeItem(const QString &filePath) {
    QFile file(filePath);
    file.remove();
}
