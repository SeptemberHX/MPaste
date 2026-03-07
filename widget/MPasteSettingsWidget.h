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
