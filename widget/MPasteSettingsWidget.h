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

signals:
    void shortcutChanged(const QString &newShortcut);

public slots:
    void accept() override;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void loadSettings();

    Ui::MPasteSettingsWidget *ui;
};

#endif // MPASTESETTINGSWIDGET_H
