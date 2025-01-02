#ifndef MPASTESETTINGSWIDGET_H
#define MPASTESETTINGSWIDGET_H

#include <QDialog>

namespace Ui {
class MPasteSettingsWidget;
}

class MPasteSettingsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit MPasteSettingsWidget(QWidget *parent = nullptr);
    ~MPasteSettingsWidget();

private:
    Ui::MPasteSettingsWidget *ui;
};

#endif // MPASTESETTINGSWIDGET_H
