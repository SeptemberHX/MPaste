#ifndef FILETHUMBWIDGET_H
#define FILETHUMBWIDGET_H

#include <QWidget>
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

private:
    Ui::FileThumbWidget *ui;
};

#endif // FILETHUMBWIDGET_H
