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
