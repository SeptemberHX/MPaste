// input: 依赖 Qt Widgets、data 层对象与同层组件声明。
// output: 对外提供 FileThumbWidget 的声明接口。
// pos: widget 层中的 FileThumbWidget 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
#ifndef FILETHUMBWIDGET_H
#define FILETHUMBWIDGET_H

#include <QWidget>
#include <QUrl>
#include <QMimeDatabase>

namespace Ui {
class FileThumbWidget;
}

class FileThumbWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FileThumbWidget(QWidget *parent = nullptr);
    ~FileThumbWidget();

    void showUrl(const QUrl &fileUrl);
    void showUrls(const QList<QUrl> &fileUrls);
    void setElidedText(const QString &str);

private:
    Ui::FileThumbWidget *ui;
    QMimeDatabase db;
};

#endif // FILETHUMBWIDGET_H
