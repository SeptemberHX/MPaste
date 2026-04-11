// input: 依赖对应头文件、Qt Widgets/布局、设置对象与自定义开关组件。
// output: 提供设置窗口的界面初始化、样式和交互逻辑实现。
// pos: widget 层中的 MPasteSettingsWidget 实现文件。
// update: 修改本文件时，同步更新文件头注释与 `widget/README.md`。
// note: Dark theme now uses light spin icons and the settings layout is grouped into clearer sections with preview cache maintenance actions.
#include "MPasteSettingsWidget.h"
#include "ui_MPasteSettingsWidget.h"
#include "utils/MPasteSettings.h"
#include "utils/ThemeManager.h"
#include "WindowBlurHelper.h"
#include "BoardInternalHelpers.h"
#include "SurfacePainter.h"
#include "ToggleSwitch.h"
#include "utils/IconResolver.h"
#include <QShowEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QGridLayout>
#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QLabel>
#include <QComboBox>
#include <QLocale>
#include <QLineEdit>
#include <QLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QDesktopServices>

static const int BORDER_WIDTH = 2;
static const int CORNER_RADIUS = 8;

namespace {
QString uiText(const char *source, const QString &zhFallback) {
    const QString translated = QObject::tr(source);
    const QLocale locale = QLocale::system();
    if (translated == QLatin1String(source) || BoardHelpers::looksBrokenTranslation(translated)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return zhFallback;
        }
        return QString::fromUtf8(source);
    }
    return translated;
}
}

static QString settingsStyleSheet(bool dark) {
    if (dark) {
        return QStringLiteral(R"(
            QDialog {
                background: transparent;
            }

            QLabel#titleLabel {
                color: #E6EDF5;
                font-size: 20px;
                font-weight: 700;
                background: transparent;
            }

            QLabel#sectionLabel {
                color: #8FB7E2;
                font-size: 11px;
                font-weight: 700;
                letter-spacing: 0.12em;
                text-transform: uppercase;
                background: transparent;
                padding: 8px 0 2px 0;
                border: none;
            }

            QFrame#generalCard {
                background-color: transparent;
                border: none;
            }


            QFrame#sep1, QFrame#sep2, QFrame#sep_autostart, QFrame#sep3, QFrame#sep4 {
                background-color: #2A313C;
                border: none;
                max-height: 1px;
            }

            QFrame#generalCard QLabel {
                color: #D6DEE8;
                font-size: 13px;
                font-weight: 400;
                background: transparent;
                padding: 0;
                border: none;
            }

            QSpinBox {
                background-color: #252B34;
                border: 1px solid #2F3945;
                border-radius: 6px;
                padding: 2px 6px;
                font-size: 13px;
                color: #E6EDF5;
                selection-background-color: #2D7FD3;
                selection-color: white;
            }
            QSpinBox:hover {
                background-color: #2A313C;
                border-color: #3A4552;
            }
            QSpinBox:focus {
                background-color: #20262F;
                border: 2px solid #2D7FD3;
                padding: 1px 5px;
            }
            QSpinBox::up-button, QSpinBox::down-button {
                width: 20px;
                border: none;
                background: transparent;
            }
            QSpinBox::up-button:hover, QSpinBox::down-button:hover {
                background-color: #2D343F;
            }
            QSpinBox::up-arrow {
                image: url(:/resources/resources/spin_up_light.svg);
                width: 10px; height: 6px;
            }
            QSpinBox::down-arrow {
                image: url(:/resources/resources/spin_down_light.svg);
                width: 10px; height: 6px;
            }

            QComboBox {
                background-color: #252B34;
                border: 1px solid #2F3945;
                border-radius: 6px;
                padding: 2px 28px 2px 8px;
                font-size: 13px;
                color: #E6EDF5;
                min-height: 28px;
            }
            QComboBox:hover {
                background-color: #2A313C;
                border-color: #3A4552;
            }
            QComboBox:focus {
                background-color: #20262F;
                border: 2px solid #2D7FD3;
                padding: 1px 27px 1px 7px;
            }
            QComboBox::drop-down {
                subcontrol-origin: padding;
                subcontrol-position: top right;
                width: 22px;
                border: none;
                background: transparent;
            }
            QComboBox::down-arrow {
                image: url(:/resources/resources/spin_down_light.svg);
                width: 10px;
                height: 6px;
            }
            QComboBox QAbstractItemView {
                background: #1E232B;
                border: 1px solid #3A4552;
                selection-background-color: #2D7FD3;
                selection-color: #FFFFFF;
            }

            QKeySequenceEdit {
                background-color: #252B34;
                border: 1px solid #2F3945;
                border-radius: 6px;
                padding: 4px 8px;
                font-size: 13px;
                color: #E6EDF5;
            }
            QKeySequenceEdit:hover {
                background-color: #2A313C;
                border-color: #3A4552;
            }
            QKeySequenceEdit:focus {
                background-color: #20262F;
                border: 2px solid #2D7FD3;
                padding: 3px 7px;
            }

            QPushButton {
                background-color: #252B34;
                border: 1px solid #2F3945;
                border-radius: 4px;
                padding: 4px 16px;
                font-size: 13px;
                font-weight: 600;
                color: #E6EDF5;
                min-width: 60px;
                min-height: 24px;
            }
            QPushButton:hover {
                background-color: #2A313C;
            }
            QPushButton:pressed {
                background-color: #1E232B;
                color: #C6D0DB;
            }

            QPushButton[text="OK"] {
                background-color: #2D7FD3;
                border: 1px solid #2D7FD3;
                color: white;
            }
            QPushButton[text="OK"]:hover {
                background-color: #276FBA;
                border-color: #276FBA;
            }
            QPushButton[text="OK"]:pressed {
                background-color: #215C9A;
                border-color: #215C9A;
            }

            QSlider::groove:horizontal {
                border: 1px solid #2F3945;
                height: 4px;
                background: #262C35;
                border-radius: 2px;
            }
            QSlider::handle:horizontal {
                background: #2D7FD3;
                border: none;
                width: 14px;
                height: 14px;
                margin: -5px 0;
                border-radius: 7px;
            }
            QSlider::handle:horizontal:hover {
                background: #276FBA;
            }

            QDialogButtonBox {
                button-layout: 2;
            }

            QLabel#section {
                color: #8FB7E2;
                font-weight: bold;
                font-size: 12px;
                background: transparent;
                padding: 2px 0;
            }

            QToolButton#closeBtn {
                color: #E6EDF5;
                background: transparent;
                border: none;
                border-radius: 11px;
                font-size: 16px;
                font-weight: bold;
            }
            QToolButton#closeBtn:hover {
                background-color: #C13B3B;
                color: white;
            }

            QToolButton#navBtn {
                background: transparent;
                border: none;
                border-radius: 8px;
                color: #8A98AB;
                font-size: 11px;
                padding: 6px 2px;
            }
            QToolButton#navBtn:checked {
                background-color: rgba(45, 127, 211, 25);
                color: #2D7FD3;
            }
            QToolButton#navBtn:hover:!checked {
                background-color: rgba(255, 255, 255, 12);
            }
            QFrame#card {
                background: rgba(255, 255, 255, 12);
                border: 1px solid rgba(255, 255, 255, 8);
                border-radius: 10px;
                padding: 6px 8px;
            }
        )");
    }
    return QStringLiteral(R"(
        QDialog {
            background: transparent;
        }

        QLabel#titleLabel {
            color: #1A1A1A;
            font-size: 20px;
            font-weight: 700;
            background: transparent;
        }

        QLabel#sectionLabel {
            color: #4A6F95;
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 0.12em;
            text-transform: uppercase;
            background: transparent;
            padding: 8px 0 2px 0;
            border: none;
        }

        QFrame#generalCard {
            background-color: transparent;
            border: none;
        }


        QFrame#sep1, QFrame#sep2, QFrame#sep_autostart, QFrame#sep3, QFrame#sep4 {
            background-color: #F0F0F0;
            border: none;
            max-height: 1px;
        }

        QFrame#generalCard QLabel {
            color: #1A1A1A;
            font-size: 13px;
            font-weight: 400;
            background: transparent;
            padding: 0;
            border: none;
        }

        QSpinBox {
            background-color: #F5F5F5;
            border: 1px solid #E0E0E0;
            border-radius: 6px;
            padding: 2px 6px;
            font-size: 13px;
            color: #1A1A1A;
            selection-background-color: #0078D4;
            selection-color: white;
        }
        QSpinBox:hover {
            background-color: #EBEBEB;
            border-color: #D0D0D0;
        }
        QSpinBox:focus {
            background-color: #FFFFFF;
            border: 2px solid #0078D4;
            padding: 1px 5px;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            width: 20px;
            border: none;
            background: transparent;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background-color: #E0E0E0;
        }
        QSpinBox::up-arrow {
            image: url(:/resources/resources/spin_up.svg);
            width: 10px; height: 6px;
        }
        QSpinBox::down-arrow {
            image: url(:/resources/resources/spin_down.svg);
            width: 10px; height: 6px;
        }

        QComboBox {
            background-color: #F5F5F5;
            border: 1px solid #E0E0E0;
            border-radius: 6px;
            padding: 2px 28px 2px 8px;
            font-size: 13px;
            color: #1A1A1A;
            min-height: 28px;
        }
        QComboBox:hover {
            background-color: #EBEBEB;
            border-color: #D0D0D0;
        }
        QComboBox:focus {
            background-color: #FFFFFF;
            border: 2px solid #0078D4;
            padding: 1px 27px 1px 7px;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 22px;
            border: none;
            background: transparent;
        }
        QComboBox::down-arrow {
            image: url(:/resources/resources/spin_down.svg);
            width: 10px;
            height: 6px;
        }
        QComboBox QAbstractItemView {
            background: #FFFFFF;
            border: 1px solid #D0D0D0;
            selection-background-color: #0078D4;
            selection-color: #FFFFFF;
        }

        QKeySequenceEdit {
            background-color: #F5F5F5;
            border: 1px solid #E0E0E0;
            border-radius: 6px;
            padding: 4px 8px;
            font-size: 13px;
            color: #1A1A1A;
        }
        QKeySequenceEdit:hover {
            background-color: #EBEBEB;
            border-color: #D0D0D0;
        }
        QKeySequenceEdit:focus {
            background-color: #FFFFFF;
            border: 2px solid #0078D4;
            padding: 3px 7px;
        }

        QPushButton {
            background-color: #FBFBFB;
            border: 1px solid #E0E0E0;
            border-radius: 4px;
            padding: 4px 16px;
            font-size: 13px;
            font-weight: 600;
            color: #1A1A1A;
            min-width: 60px;
            min-height: 24px;
        }
        QPushButton:hover {
            background-color: #F0F0F0;
        }
        QPushButton:pressed {
            background-color: #E5E5E5;
            color: #444;
        }

        QPushButton[text="OK"] {
            background-color: #0078D4;
            border: 1px solid #0078D4;
            color: white;
        }
        QPushButton[text="OK"]:hover {
            background-color: #006CBC;
            border-color: #006CBC;
        }
        QPushButton[text="OK"]:pressed {
            background-color: #005499;
            border-color: #005499;
        }

        QSlider::groove:horizontal {
            border: 1px solid #E0E0E0;
            height: 4px;
            background: #F0F0F0;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #0078D4;
            border: none;
            width: 14px;
            height: 14px;
            margin: -5px 0;
            border-radius: 7px;
        }
        QSlider::handle:horizontal:hover {
            background: #006CBC;
        }

        QDialogButtonBox {
            button-layout: 2;
        }

        QLabel#section {
            color: #2A6CB0;
            font-weight: bold;
            font-size: 12px;
            background: transparent;
            padding: 2px 0;
        }

        QToolButton#closeBtn {
            color: #1C2330;
            background: transparent;
            border: none;
            border-radius: 11px;
            font-size: 16px;
            font-weight: bold;
        }
        QToolButton#closeBtn:hover {
            background-color: #C13B3B;
            color: white;
        }

        QToolButton#navBtn {
            background: transparent;
            border: none;
            border-radius: 8px;
            color: #6A7888;
            font-size: 11px;
            padding: 6px 2px;
        }
        QToolButton#navBtn:checked {
            background-color: rgba(45, 127, 211, 18);
            color: #2D7FD3;
        }
        QToolButton#navBtn:hover:!checked {
            background-color: rgba(0, 0, 0, 6);
        }
        QFrame#card {
            background: rgba(255, 255, 255, 180);
            border: 1px solid rgba(0, 0, 0, 6);
            border-radius: 10px;
            padding: 6px 8px;
        }
    )");
}

MPasteSettingsWidget::MPasteSettingsWidget(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MPasteSettingsWidget)
{
    ui->setupUi(this);

    setMinimumWidth(580);
    setMaximumWidth(580);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    if (layout()) {
        layout()->setSizeConstraint(QLayout::SetMinAndMaxSize);
    }

    // Frameless + translucent for custom-painted gradient border
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &MPasteSettingsWidget::applyTheme);

    setWindowTitle(uiText("Settings", QStringLiteral("设置")));
    ui->titleLabel->hide(); // replaced by custom title bar

    // ── Custom title bar (MTodo pattern) ──
    {
        auto *titleBar = new QWidget(this);
        titleBar->setFixedHeight(32);
        titleBar->setCursor(Qt::SizeAllCursor);
        titleBar->setObjectName(QStringLiteral("titleBar"));
        auto *tb = new QHBoxLayout(titleBar);
        tb->setContentsMargins(8, 0, 4, 0);
        tb->setSpacing(6);
        auto *titleLabel = new QLabel(uiText("MPaste Settings", QStringLiteral("MPaste 设置")), titleBar);
        QFont f = titleLabel->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() + 0.5);
        titleLabel->setFont(f);
        tb->addWidget(titleLabel);
        tb->addStretch();
        auto *closeBtn = new QToolButton(titleBar);
        closeBtn->setText(QStringLiteral("\u00D7"));
        closeBtn->setFocusPolicy(Qt::NoFocus);
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setFixedSize(22, 22);
        closeBtn->setObjectName(QStringLiteral("closeBtn"));
        connect(closeBtn, &QToolButton::clicked, this, &QDialog::close);
        tb->addWidget(closeBtn);
        ui->mainLayout->insertWidget(0, titleBar);
    }
    if (auto *grid = qobject_cast<QGridLayout*>(ui->generalCard->layout())) {
        grid->removeWidget(ui->label);
        grid->removeWidget(ui->numSpinBox);
    }
    ui->label->hide();
    ui->numSpinBox->hide();
    ui->sep1->show();
    ui->label_2->setText(uiText("Retention period", QStringLiteral("保留时长")));
    ui->label_autostart->setText(uiText("Launch at startup", QStringLiteral("开机自启动")));
    ui->label_3->setText(uiText("Play copy sound", QStringLiteral("播放复制提示音")));
    ui->label_4->setText(uiText("Activation shortcut", QStringLiteral("唤起快捷键")));
    ui->label_5->setText(uiText("Card size", QStringLiteral("卡片大小")));

    // Replace the placeholder QCheckBoxes with proper ToggleSwitches
    autoStartSwitch_ = new ToggleSwitch(this);
    toggleSwitch_ = new ToggleSwitch(this);
    auto *grid = qobject_cast<QGridLayout*>(ui->generalCard->layout());
    if (grid) {
        grid->removeWidget(ui->autoStartCheckBox);
        ui->autoStartCheckBox->hide();
        grid->addWidget(autoStartSwitch_, 4, 1, Qt::AlignRight | Qt::AlignVCenter);

        grid->removeWidget(ui->playSoundCheckBox);
        ui->playSoundCheckBox->hide();
        grid->addWidget(toggleSwitch_, 6, 1, Qt::AlignRight | Qt::AlignVCenter);

        themeLabel_ = new QLabel(uiText("Theme", QStringLiteral("主题")), this);
        themeLabel_->setMinimumHeight(44);
        themeCombo_ = new QComboBox(this);
        themeCombo_->setMinimumSize(QSize(140, 32));
        themeCombo_->setMaximumHeight(32);
        themeCombo_->addItem(uiText("Follow system", QStringLiteral("跟随系统")), static_cast<int>(MPasteSettings::ThemeSystem));
        themeCombo_->addItem(uiText("Light", QStringLiteral("浅色")), static_cast<int>(MPasteSettings::ThemeLight));
        themeCombo_->addItem(uiText("Dark", QStringLiteral("暗色")), static_cast<int>(MPasteSettings::ThemeDark));
        grid->addWidget(themeLabel_, 0, 0);
        grid->addWidget(themeCombo_, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
        connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this]() {
            if (!themeCombo_) {
                return;
            }
            const auto mode = static_cast<MPasteSettings::ThemeMode>(themeCombo_->currentData().toInt());
            const bool dark = mode == MPasteSettings::ThemeDark
                || (mode == MPasteSettings::ThemeSystem && MPasteSettings::getInst()->isDarkTheme());
            applyTheme(dark);
        });

        pasteShortcutLabel_ = new QLabel(uiText("Auto-paste shortcut", QStringLiteral("自动粘贴快捷键")), this);
        pasteShortcutLabel_->setMinimumHeight(44);
        pasteShortcutCombo_ = new QComboBox(this);
        pasteShortcutCombo_->setMinimumSize(QSize(140, 32));
        pasteShortcutCombo_->setMaximumHeight(32);
        pasteShortcutCombo_->addItem(uiText("Auto (Recommended)", QStringLiteral("自动（推荐）")), static_cast<int>(MPasteSettings::AutoPasteShortcut));
        pasteShortcutCombo_->addItem(QStringLiteral("Ctrl+V"), static_cast<int>(MPasteSettings::CtrlVShortcut));
        pasteShortcutCombo_->addItem(QStringLiteral("Shift+Insert"), static_cast<int>(MPasteSettings::ShiftInsertShortcut));
        pasteShortcutCombo_->addItem(QStringLiteral("Ctrl+Shift+V"), static_cast<int>(MPasteSettings::CtrlShiftVShortcut));
        pasteShortcutCombo_->addItem(QStringLiteral("Alt+Insert"), static_cast<int>(MPasteSettings::AltInsertShortcut));
        grid->addWidget(pasteShortcutLabel_, 12, 0);
        grid->addWidget(pasteShortcutCombo_, 12, 1, Qt::AlignRight | Qt::AlignVCenter);

        auto *retentionWidget = new QWidget(this);
        auto *retentionLayout = new QHBoxLayout(retentionWidget);
        retentionLayout->setContentsMargins(0, 0, 0, 0);
        retentionLayout->setSpacing(8);
        grid->removeWidget(ui->daySpinBox);
        ui->daySpinBox->setMinimumSize(QSize(72, 32));
        ui->daySpinBox->setMaximumSize(QSize(72, 32));
        retentionLayout->addWidget(ui->daySpinBox);
        retentionUnitCombo_ = new QComboBox(this);
        retentionUnitCombo_->setMinimumSize(QSize(92, 32));
        retentionUnitCombo_->setMaximumHeight(32);
        retentionUnitCombo_->addItem(uiText("Days", QStringLiteral("天")), static_cast<int>(MPasteSettings::RetentionDays));
        retentionUnitCombo_->addItem(uiText("Weeks", QStringLiteral("周")), static_cast<int>(MPasteSettings::RetentionWeeks));
        retentionUnitCombo_->addItem(uiText("Months", QStringLiteral("月")), static_cast<int>(MPasteSettings::RetentionMonths));
        retentionLayout->addWidget(retentionUnitCombo_);
        grid->addWidget(retentionWidget, 2, 1, Qt::AlignRight | Qt::AlignVCenter);

        auto *syncSep = new QFrame(this);
        syncSep->setMaximumHeight(1);
        syncSep->setFrameShape(QFrame::HLine);
        grid->addWidget(syncSep, 13, 0, 1, 2);

        syncLabel_ = new QLabel(uiText("Sync folder", QStringLiteral("同步目录")), this);
        syncLabel_->setMinimumHeight(44);
        syncPathEdit_ = new QLineEdit(this);
        syncPathEdit_->setReadOnly(true);
        syncPathEdit_->setMinimumHeight(32);
        syncPathEdit_->setPlaceholderText(uiText("Select a folder to sync", QStringLiteral("选择同步目录")));
        syncPathEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        grid->addWidget(syncLabel_, 14, 0);
        grid->addWidget(syncPathEdit_, 14, 1);

        auto *syncButtonsRow = new QWidget(this);
        auto *syncButtonsLayout = new QHBoxLayout(syncButtonsRow);
        syncButtonsLayout->setContentsMargins(0, 0, 0, 0);
        syncButtonsLayout->setSpacing(6);
        syncButtonsLayout->addStretch(1);

        syncOpenButton_ = new QPushButton(uiText("Open", QStringLiteral("打开")), this);
        syncOpenButton_->setMinimumSize(QSize(64, 36));
        syncOpenButton_->setMaximumHeight(36);
        syncButtonsLayout->addWidget(syncOpenButton_);

        syncChangeButton_ = new QPushButton(uiText("Change", QStringLiteral("修改")), this);
        syncChangeButton_->setMinimumSize(QSize(76, 36));
        syncChangeButton_->setMaximumHeight(36);
        syncButtonsLayout->addWidget(syncChangeButton_);

        grid->addWidget(syncButtonsRow, 15, 1, Qt::AlignRight | Qt::AlignVCenter);

        // WebDAV sync UI intentionally omitted; external sync tools are recommended.

        connect(syncChangeButton_, &QPushButton::clicked, this, [this]() {
            const QString currentDir = syncPathEdit_ ? syncPathEdit_->text() : QString();
            const QString selected = QFileDialog::getExistingDirectory(
                this,
                uiText("Select sync folder", QStringLiteral("选择同步目录")),
                currentDir.isEmpty() ? QDir::homePath() : currentDir);
            if (!selected.isEmpty() && syncPathEdit_) {
                syncPathEdit_->setText(QDir::cleanPath(selected));
            }
        });

        connect(syncOpenButton_, &QPushButton::clicked, this, [this]() {
            const QString path = syncPathEdit_ ? syncPathEdit_->text().trimmed() : QString();
            if (path.isEmpty()) {
                return;
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });

        grid->setContentsMargins(18, 14, 18, 14);
        grid->setHorizontalSpacing(14);
        grid->setVerticalSpacing(8);
        grid->setColumnStretch(0, 0);
        grid->setColumnStretch(1, 1);

        ui->sep1->hide();
        ui->sep2->hide();
        ui->sep_autostart->hide();
        ui->sep3->hide();
        ui->sep4->hide();
        syncSep->hide();

        syncLabel_->setMinimumHeight(24);
        syncPathEdit_->setMinimumHeight(36);
        themeCombo_->setMinimumHeight(36);
        themeCombo_->setMaximumHeight(36);
        pasteShortcutCombo_->setMinimumHeight(36);
        pasteShortcutCombo_->setMaximumHeight(36);
        retentionUnitCombo_->setMinimumHeight(36);
        retentionUnitCombo_->setMaximumHeight(36);
        ui->daySpinBox->setMinimumHeight(36);
        ui->daySpinBox->setMaximumHeight(36);
        ui->shortcutEdit->setMinimumHeight(36);
        ui->shortcutEdit->setMaximumHeight(36);
        syncButtonsLayout->setSpacing(8);

        // ── Sidebar + StackedWidget (Clipaste-style) ──

        auto createPageLayout = [](QWidget *parent) {
            auto *layout = new QVBoxLayout(parent);
            layout->setContentsMargins(16, 10, 16, 10);
            layout->setSpacing(12);
            return layout;
        };

        auto makeSectionLabel = [](const QString &text, QWidget *parent) {
            auto *label = new QLabel(text, parent);
            label->setObjectName(QStringLiteral("section"));
            return label;
        };

        auto makeCard = [](QLayout *layout) {
            auto *card = new QFrame();
            card->setObjectName(QStringLiteral("card"));
            card->setLayout(layout);
            return card;
        };

        // ── Sidebar with QToolButtons (MTodo pattern) ──
        auto *sideWidget = new QWidget(ui->generalCard);
        sideWidget->setFixedWidth(80);
        auto *sideLayout = new QVBoxLayout(sideWidget);
        sideLayout->setContentsMargins(0, 8, 0, 8);
        sideLayout->setSpacing(6);

        auto *navGroup = new QButtonGroup(this);
        navGroup->setExclusive(true);

        auto makeNavBtn = [&](const QString &iconName, const QString &text, int id) {
            auto *btn = new QToolButton();
            btn->setObjectName(QStringLiteral("navBtn"));
            btn->setIcon(IconResolver::themedIcon(iconName, ThemeManager::instance()->isDark()));
            btn->setIconSize(QSize(22, 22));
            btn->setText(text);
            btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            btn->setCheckable(true);
            btn->setFixedSize(72, 52);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFocusPolicy(Qt::NoFocus);
            navGroup->addButton(btn, id);
            sideLayout->addWidget(btn, 0, Qt::AlignHCenter);
        };

        makeNavBtn(QStringLiteral("settings"), uiText("General", QStringLiteral("通用")), 0);
        makeNavBtn(QStringLiteral("rename"), uiText("Shortcuts", QStringLiteral("快捷键")), 1);
        makeNavBtn(QStringLiteral("menu_more"), uiText("Advanced", QStringLiteral("高级")), 2);
        sideLayout->addStretch();
        makeNavBtn(QStringLiteral("info"), uiText("About", QStringLiteral("关于")), 3);

        auto *stack = new QStackedWidget(ui->generalCard);
        stack->setObjectName(QStringLiteral("settingsStack"));

        // ── General page ──
        auto *generalPage = new QWidget(stack);
        auto *gLayout = createPageLayout(generalPage);

        auto makeSettingsGrid = []() {
            auto *g = new QGridLayout();
            g->setContentsMargins(14, 10, 14, 10);
            g->setHorizontalSpacing(12);
            g->setVerticalSpacing(12);
            g->setColumnStretch(0, 1);
            g->setColumnStretch(1, 0);
            return g;
        };

        gLayout->addWidget(makeSectionLabel(uiText("Appearance", QStringLiteral("外观")), generalPage));
        {
            auto *g = makeSettingsGrid();
            g->addWidget(themeLabel_, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(themeCombo_, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
            g->addWidget(ui->label_autostart, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(autoStartSwitch_, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
            gLayout->addWidget(makeCard(g));
        }

        gLayout->addWidget(makeSectionLabel(uiText("Behavior", QStringLiteral("行为")), generalPage));
        {
            auto *g = makeSettingsGrid();
            g->addWidget(ui->label_3, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(toggleSwitch_, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
            g->addWidget(ui->label_2, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(retentionWidget, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
            g->addWidget(ui->label_5, 2, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(ui->scaleWidget, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
            gLayout->addWidget(makeCard(g));
        }

        gLayout->addStretch();
        stack->addWidget(generalPage);

        // ── Shortcuts page ──
        auto *shortcutsPage = new QWidget(stack);
        auto *sLayout = createPageLayout(shortcutsPage);

        sLayout->addWidget(makeSectionLabel(uiText("Shortcuts", QStringLiteral("快捷键")), shortcutsPage));
        {
            auto *g = makeSettingsGrid();
            g->addWidget(ui->label_4, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(ui->shortcutEdit, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
            g->addWidget(pasteShortcutLabel_, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(pasteShortcutCombo_, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
            sLayout->addWidget(makeCard(g));
        }

        sLayout->addStretch();
        stack->addWidget(shortcutsPage);

        // ── Advanced page ──
        auto *maintenancePage = new QWidget(stack);
        auto *mLayout = createPageLayout(maintenancePage);

        mLayout->addWidget(makeSectionLabel(uiText("Sync", QStringLiteral("同步")), maintenancePage));
        {
            auto *g = makeSettingsGrid();
            g->addWidget(syncLabel_, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(syncPathEdit_, 0, 1);
            g->addWidget(syncButtonsRow, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
            mLayout->addWidget(makeCard(g));
        }

        ocrLabel_ = new QLabel(uiText("OCR Backend", QStringLiteral("OCR 引擎")), maintenancePage);
        ocrBackendCombo_ = new QComboBox(maintenancePage);
        ocrBackendCombo_->setMinimumSize(QSize(180, 36));
        ocrBackendCombo_->setMaximumHeight(36);
        ocrBackendCombo_->addItem(uiText("Windows Built-in", QStringLiteral("Windows 内置")), 0);
        ocrBackendCombo_->addItem(uiText("Baidu OCR API", QStringLiteral("百度 OCR API")), 1);

        baiduApiKeyLabel_ = new QLabel(QStringLiteral("API Key"), maintenancePage);
        baiduApiKeyEdit_ = new QLineEdit(maintenancePage);
        baiduApiKeyEdit_->setMinimumHeight(36);
        baiduApiKeyEdit_->setPlaceholderText(uiText("Enter Baidu API Key", QStringLiteral("输入百度 API Key")));

        baiduSecretKeyLabel_ = new QLabel(QStringLiteral("Secret Key"), maintenancePage);
        baiduSecretKeyEdit_ = new QLineEdit(maintenancePage);
        baiduSecretKeyEdit_->setMinimumHeight(36);
        baiduSecretKeyEdit_->setEchoMode(QLineEdit::Password);
        baiduSecretKeyEdit_->setPlaceholderText(uiText("Enter Baidu Secret Key", QStringLiteral("输入百度 Secret Key")));

        auto updateBaiduFieldsVisibility = [this]() {
            const bool isBaidu = ocrBackendCombo_->currentData().toInt() == 1;
            baiduApiKeyLabel_->setVisible(isBaidu);
            baiduApiKeyEdit_->setVisible(isBaidu);
            baiduSecretKeyLabel_->setVisible(isBaidu);
            baiduSecretKeyEdit_->setVisible(isBaidu);
        };
        connect(ocrBackendCombo_, &QComboBox::currentIndexChanged, this, updateBaiduFieldsVisibility);

        autoOcrLabel_ = new QLabel(uiText("Auto OCR for images", QStringLiteral("图片自动 OCR")), maintenancePage);
        autoOcrSwitch_ = new ToggleSwitch(maintenancePage);

        mLayout->addWidget(makeSectionLabel(uiText("OCR", QStringLiteral("OCR")), maintenancePage));
        {
            auto *g = makeSettingsGrid();
            g->addWidget(ocrLabel_, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(ocrBackendCombo_, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
            g->addWidget(baiduApiKeyLabel_, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(baiduApiKeyEdit_, 1, 1);
            g->addWidget(baiduSecretKeyLabel_, 2, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(baiduSecretKeyEdit_, 2, 1);
            g->addWidget(autoOcrLabel_, 3, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(autoOcrSwitch_, 3, 1, Qt::AlignRight | Qt::AlignVCenter);
            mLayout->addWidget(makeCard(g));
        }

        mLayout->addStretch();
        stack->addWidget(maintenancePage);

        // ── About page ──
        auto *aboutPage = new QWidget(stack);
        auto *aboutLayout = new QVBoxLayout(aboutPage);
        aboutLayout->setContentsMargins(20, 24, 20, 20);
        aboutLayout->setSpacing(12);
        auto *logoLabel = new QLabel(aboutPage);
        logoLabel->setPixmap(QPixmap(QStringLiteral(":/resources/resources/mpaste.svg")).scaled(
            64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        logoLabel->setAlignment(Qt::AlignCenter);
        aboutLayout->addWidget(logoLabel);
        auto *nameLabel = new QLabel(QStringLiteral("MPaste V" MPASTE_VERSION), aboutPage);
        nameLabel->setAlignment(Qt::AlignCenter);
        nameLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; background: transparent;"));
        aboutLayout->addWidget(nameLabel);
        auto *authorLabel = new QLabel(
            QStringLiteral("Author: SeptemberHX<br>"
                           "Github: <a href=\"https://github.com/SeptemberHX/MPaste\">SeptemberHX/MPaste</a><br>"
                           "Email: <a href=\"mailto:september_hx@outlook.com\">september_hx@outlook.com</a>"),
            aboutPage);
        authorLabel->setAlignment(Qt::AlignCenter);
        authorLabel->setOpenExternalLinks(true);
        authorLabel->setStyleSheet(QStringLiteral("font-size: 13px; background: transparent; line-height: 1.6;"));
        aboutLayout->addWidget(authorLabel);
        aboutLayout->addStretch(1);
        stack->addWidget(aboutPage);

        // ── Wire sidebar ↔ stack ──
        connect(navGroup, &QButtonGroup::idClicked,
                stack, &QStackedWidget::setCurrentIndex);
        if (auto *btn = navGroup->button(0))
            btn->setChecked(true);
        stack->setCurrentIndex(0);

        // ── Layout: sidebar | content (no separator line) ──
        auto *hbox = new QHBoxLayout;
        hbox->setContentsMargins(0, 0, 0, 0);
        hbox->setSpacing(0);
        hbox->addWidget(sideWidget);
        hbox->addWidget(stack, 1);

        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(0);
        grid->setVerticalSpacing(0);
        grid->setColumnStretch(0, 1);
        grid->addLayout(hbox, 0, 0, 1, 2);
    }

#ifndef Q_OS_WIN
    const QString autoStartTip = tr("Auto-start is currently only supported on Windows.");
    ui->label_autostart->setEnabled(false);
    ui->label_autostart->setToolTip(autoStartTip);
    autoStartSwitch_->setChecked(false);
    autoStartSwitch_->setEnabled(false);
    autoStartSwitch_->setToolTip(autoStartTip);
#endif

    // Connect slider to label
    connect(ui->itemScaleSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->scaleValueLabel->setText(QString("%1%").arg(value));
    });

    loadSettings();

    // ── Auto-save: every control change saves immediately ──
    auto save = [this]() { saveAllSettings(); };
    connect(toggleSwitch_, &ToggleSwitch::toggled, this, save);
    connect(autoStartSwitch_, &ToggleSwitch::toggled, this, save);
    if (themeCombo_) connect(themeCombo_, &QComboBox::currentIndexChanged, this, save);
    if (retentionUnitCombo_) connect(retentionUnitCombo_, &QComboBox::currentIndexChanged, this, save);
    if (pasteShortcutCombo_) connect(pasteShortcutCombo_, &QComboBox::currentIndexChanged, this, save);
    if (ocrBackendCombo_) connect(ocrBackendCombo_, &QComboBox::currentIndexChanged, this, save);
    if (autoOcrSwitch_) connect(autoOcrSwitch_, &ToggleSwitch::toggled, this, save);
    connect(ui->daySpinBox, &QSpinBox::valueChanged, this, save);
    connect(ui->itemScaleSlider, &QSlider::valueChanged, this, save);
    connect(ui->shortcutEdit, &QKeySequenceEdit::keySequenceChanged, this, save);

    adjustSize();
}

MPasteSettingsWidget::~MPasteSettingsWidget()
{
    delete ui;
}

void MPasteSettingsWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    SurfacePainter::paintPanel(p, QRectF(rect()), 12.0, darkTheme_, 45);
}

void MPasteSettingsWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Only drag from the top 42px (title bar area)
        if (event->position().toPoint().y() <= 42) {
            dragPos_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
        event->accept();
    }
}

void MPasteSettingsWidget::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !dragPos_.isNull()) {
        move(event->globalPosition().toPoint() - dragPos_);
        event->accept();
    }
}

void MPasteSettingsWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragPos_ = QPoint();
    }
    QDialog::mouseReleaseEvent(event);
}

void MPasteSettingsWidget::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    loadSettings();
    adjustSize();
    resize(width(), sizeHint().height());
}

void MPasteSettingsWidget::loadSettings()
{
    auto *settings = MPasteSettings::getInst();
    ui->daySpinBox->setValue(settings->getHistoryRetentionValue());
    if (retentionUnitCombo_) {
        const int index = retentionUnitCombo_->findData(static_cast<int>(settings->getHistoryRetentionUnit()));
        retentionUnitCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    ui->shortcutEdit->setKeySequence(QKeySequence(settings->getShortcutStr()));
    if (pasteShortcutCombo_) {
        const int index = pasteShortcutCombo_->findData(static_cast<int>(settings->getPasteShortcutMode()));
        pasteShortcutCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    ui->itemScaleSlider->setValue(settings->getItemScale());
    ui->scaleValueLabel->setText(QString("%1%").arg(settings->getItemScale()));
    toggleSwitch_->setChecked(settings->isPlaySound());
    if (themeCombo_) {
        const int index = themeCombo_->findData(static_cast<int>(settings->getThemeMode()));
        themeCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (syncPathEdit_) {
        syncPathEdit_->setText(QDir::cleanPath(settings->getSaveDir()));
    }
    if (ocrBackendCombo_) {
        const int index = ocrBackendCombo_->findData(static_cast<int>(settings->getOcrBackend()));
        ocrBackendCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (baiduApiKeyEdit_) {
        baiduApiKeyEdit_->setText(settings->getBaiduOcrApiKey());
    }
    if (baiduSecretKeyEdit_) {
        baiduSecretKeyEdit_->setText(settings->getBaiduOcrSecretKey());
    }
    if (autoOcrSwitch_) {
        autoOcrSwitch_->setChecked(settings->isAutoOcr());
    }
    // Trigger visibility update for Baidu fields.
    if (ocrBackendCombo_) {
        const bool isBaidu = ocrBackendCombo_->currentData().toInt() == 1;
        if (baiduApiKeyLabel_) baiduApiKeyLabel_->setVisible(isBaidu);
        if (baiduApiKeyEdit_) baiduApiKeyEdit_->setVisible(isBaidu);
        if (baiduSecretKeyLabel_) baiduSecretKeyLabel_->setVisible(isBaidu);
        if (baiduSecretKeyEdit_) baiduSecretKeyEdit_->setVisible(isBaidu);
    }
    applyTheme(ThemeManager::instance()->isDark());

#ifdef Q_OS_WIN
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    autoStartSwitch_->setChecked(reg.contains("MPaste"));
#else
    autoStartSwitch_->setChecked(false);
#endif
}

void MPasteSettingsWidget::accept() {
    saveAllSettings();
    QDialog::accept();
}

void MPasteSettingsWidget::saveAllSettings()
{
    auto *settings = MPasteSettings::getInst();
    const int oldRetentionValue = settings->getHistoryRetentionValue();
    const auto oldRetentionUnit = settings->getHistoryRetentionUnit();
    const int oldScale = settings->getItemScale();
    const int newRetentionValue = ui->daySpinBox->value();
    const auto newRetentionUnit = retentionUnitCombo_
        ? static_cast<MPasteSettings::HistoryRetentionUnit>(retentionUnitCombo_->currentData().toInt())
        : MPasteSettings::RetentionDays;
    settings->setHistoryRetentionValue(newRetentionValue);
    settings->setHistoryRetentionUnit(newRetentionUnit);

    QString newShortcut = ui->shortcutEdit->keySequence().toString();
    if (!newShortcut.isEmpty() && newShortcut != settings->getShortcutStr()) {
        settings->setShortcutStr(newShortcut);
        emit shortcutChanged(newShortcut);
    }

    if (pasteShortcutCombo_) {
        settings->setPasteShortcutMode(static_cast<MPasteSettings::PasteShortcutMode>(pasteShortcutCombo_->currentData().toInt()));
    }
    const int newScale = ui->itemScaleSlider->value();
    settings->setItemScale(newScale);
    settings->setPlaySound(toggleSwitch_->isChecked());
    if (themeCombo_) {
        const auto mode = static_cast<MPasteSettings::ThemeMode>(themeCombo_->currentData().toInt());
        if (settings->getThemeMode() != mode) {
            settings->setThemeMode(mode);
            emit themeChanged();
        }
    }

    if (syncPathEdit_) {
        const QString newDir = QDir::cleanPath(syncPathEdit_->text().trimmed());
        if (!newDir.isEmpty() && newDir != settings->getSaveDir()) {
            QDir dir(newDir);
            if (!dir.exists()) {
                dir.mkpath(QStringLiteral("."));
            }
            settings->setSaveDir(newDir);
            emit saveDirChanged();
        }
    }

    if (ocrBackendCombo_) {
        settings->setOcrBackend(static_cast<MPasteSettings::OcrBackend>(ocrBackendCombo_->currentData().toInt()));
    }
    if (baiduApiKeyEdit_) {
        settings->setBaiduOcrApiKey(baiduApiKeyEdit_->text().trimmed());
    }
    if (baiduSecretKeyEdit_) {
        settings->setBaiduOcrSecretKey(baiduSecretKeyEdit_->text().trimmed());
    }
    if (autoOcrSwitch_) {
        settings->setAutoOcr(autoOcrSwitch_->isChecked());
    }

#ifdef Q_OS_WIN
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    if (autoStartSwitch_->isChecked()) {
        const QString exePath = QDir::toNativeSeparators(qApp->applicationFilePath());
        reg.setValue("MPaste", QStringLiteral("\"%1\"").arg(exePath));
    } else {
        reg.remove("MPaste");
    }
#endif

    settings->saveSettings();
    if (oldRetentionValue != newRetentionValue || oldRetentionUnit != newRetentionUnit) {
        emit historyRetentionChanged();
    }
    if (oldScale != newScale) {
        emit itemScaleChanged(newScale);
    }
}

void MPasteSettingsWidget::applyTheme(bool dark) {
    darkTheme_ = dark;
    WindowBlurHelper::enableBlurBehind(this, darkTheme_);
    setStyleSheet(settingsStyleSheet(darkTheme_));

    // Update sidebar icons for the new theme.
    static const QStringList navIcons = {
        QStringLiteral("settings"), QStringLiteral("rename"),
        QStringLiteral("menu_more"), QStringLiteral("info")
    };
    const auto navBtns = findChildren<QToolButton *>(QStringLiteral("navBtn"));
    for (int i = 0; i < navBtns.size() && i < navIcons.size(); ++i) {
        navBtns[i]->setIcon(IconResolver::themedIcon(navIcons[i], dark));
    }

    update();
}
