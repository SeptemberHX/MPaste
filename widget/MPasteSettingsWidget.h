// input: 依赖 Qt Widgets、data 层对象与同层组件声明。
// output: 对外提供 MPasteSettingsWidget 的声明接口。
// pos: widget 层中的 MPasteSettingsWidget 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
#ifndef MPASTESETTINGSWIDGET_H
#define MPASTESETTINGSWIDGET_H

#include <QDialog>
#include <QPoint>

class ToggleSwitch;

namespace Ui {
class MPasteSettingsWidget;
}

class MPasteSettingsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit MPasteSettingsWidget(QWidget *parent = nullptr);
    ~MPasteSettingsWidget();

signals:
    void shortcutChanged(const QString &newShortcut);

public slots:
    void accept() override;

protected:
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void loadSettings();

    Ui::MPasteSettingsWidget *ui;
    ToggleSwitch *toggleSwitch_;
    ToggleSwitch *autoStartSwitch_;
    QPoint dragPos_;
};

#endif // MPASTESETTINGSWIDGET_H
