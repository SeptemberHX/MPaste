// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 FileThumbWidget 的声明接口，对图片文件走缩放预览，其余文件走图标预览。
// pos: widget 层中的 FileThumbWidget 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#ifndef FILETHUMBWIDGET_H
#define FILETHUMBWIDGET_H

#include <QWidget>
#include <QMimeDatabase>
#include <QUrl>

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
    void setPathVisible(bool visible);

    Ui::FileThumbWidget *ui;
    QMimeDatabase mimeDatabase_;
};

#endif // FILETHUMBWIDGET_H
