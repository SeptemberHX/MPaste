// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 MPasteSettingsWidget 的声明接口。
// pos: widget 层中的 MPasteSettingsWidget 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#ifndef MPASTESETTINGSWIDGET_H
#define MPASTESETTINGSWIDGET_H

#include <QDialog>
#include <QPoint>

class ToggleSwitch;
class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;

namespace Ui {
class MPasteSettingsWidget;
}

class MPasteSettingsWidget : public QDialog
{
    Q_OBJECT

public:
    enum PreviewCacheAction {
        RepairBrokenPreviews = 0,
        RebuildCurrentPreviews,
        ClearCurrentPreviews
    };
    Q_ENUM(PreviewCacheAction)

    explicit MPasteSettingsWidget(QWidget *parent = nullptr);
    ~MPasteSettingsWidget();
    void applyTheme(bool dark);

signals:
    void shortcutChanged(const QString &newShortcut);
    void historyRetentionChanged();
    void themeChanged();
    void saveDirChanged();
    void itemScaleChanged(int itemScale);
    void thumbnailPrefetchChanged(int count);
    void previewCacheActionRequested(PreviewCacheAction action);

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
    QLabel *pasteShortcutLabel_ = nullptr;
    QComboBox *pasteShortcutCombo_ = nullptr;
    QLabel *themeLabel_ = nullptr;
    QComboBox *themeCombo_ = nullptr;
    QComboBox *retentionUnitCombo_ = nullptr;
    QLabel *syncLabel_ = nullptr;
    QLineEdit *syncPathEdit_ = nullptr;
    QPushButton *syncOpenButton_ = nullptr;
    QPushButton *syncChangeButton_ = nullptr;
    QLabel *previewCacheLabel_ = nullptr;
    QPushButton *previewRepairButton_ = nullptr;
    QPushButton *previewRebuildButton_ = nullptr;
    QPushButton *previewClearButton_ = nullptr;
    QPoint dragPos_;
    bool darkTheme_ = false;
};

#endif // MPASTESETTINGSWIDGET_H
