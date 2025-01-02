#include "MPasteSettingsWidget.h"
#include "ui_MPasteSettingsWidget.h"


QString getWin11Style() {
    return R"(
        QDialog {
            background-color: #ffffff;
            border: 1px solid #E4E4E4;
            border-radius: 8px;
        }

        QLabel {
            color: #202020;
            font-size: 14px;
            font-weight: 600;  /* 增加字体粗细 */
        }

        QSpinBox {
            background-color: #ffffff;
            border: 1px solid #D1D1D1;
            border-radius: 4px;
            padding: 2px 5px;  /* 减小上下内边距 */
            min-height: 24px;  /* 减小最小高度 */
        }

        QSpinBox:hover {
            border-color: #0078D4;
        }

        QSpinBox:focus {
            border-color: #0078D4;
            border-width: 2px;
        }

        QSpinBox::up-button, QSpinBox::down-button {
            width: 20px;
            border: none;
            background: transparent;
        }

        QSpinBox::up-arrow {
            image: url(:/icons/up-arrow.png);  /* 需要提供对应图标 */
        }

        QSpinBox::down-arrow {
            image: url(:/icons/down-arrow.png);  /* 需要提供对应图标 */
        }

        QCheckBox {
            spacing: 8px;
        }

        QCheckBox::indicator {
            width: 20px;
            height: 20px;
            border: 1px solid #D1D1D1;
            border-radius: 4px;
        }

        QCheckBox::indicator:hover {
            border-color: #0078D4;
        }

        QCheckBox::indicator:checked {
            background-color: #0078D4;
            border-color: #0078D4;
            image: url(:/icons/checkmark.png);  /* 需要提供对应图标 */
        }

        QDialogButtonBox {
            button-layout: 2;  /* 右对齐按钮 */
        }

        QPushButton {
            background-color: #FFFFFF;
            border: 1px solid #D1D1D1;
            border-radius: 4px;
            padding: 6px 22px;  /* 稍微调整按钮内边距 */
            color: #202020;
            font-size: 14px;
            font-weight: 600;  /* 增加按钮文字粗细 */
            min-height: 32px;
        }

        QPushButton:hover {
            background-color: #F5F5F5;
            border-color: #D1D1D1;
        }

        QPushButton:pressed {
            background-color: #E5E5E5;
        }

        /* OK 按钮特殊样式 */
        QPushButton[text="OK"] {
            background-color: #0078D4;
            border-color: #0078D4;
            color: white;
        }

        QPushButton[text="OK"]:hover {
            background-color: #006CBC;
        }

        QPushButton[text="OK"]:pressed {
            background-color: #005AA3;
        }
    )";
}

MPasteSettingsWidget::MPasteSettingsWidget(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MPasteSettingsWidget)
{
    ui->setupUi(this);

    // 应用样式表
    setStyleSheet(getWin11Style());
}

MPasteSettingsWidget::~MPasteSettingsWidget()
{
    delete ui;
}
