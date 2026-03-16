// input: 依赖对应头文件、Qt Widgets/布局、设置对象与自定义开关组件。
// output: 提供设置窗口的界面初始化、样式和交互逻辑实现。
// pos: widget 层中的 MPasteSettingsWidget 实现文件。
// update: 修改本文件时，同步更新文件头注释与 `widget/README.md`。
// note: Dark theme now uses light spin icons.
#include "MPasteSettingsWidget.h"
#include "ui_MPasteSettingsWidget.h"
#include "utils/MPasteSettings.h"
#include "utils/ThemeManager.h"
#include "ToggleSwitch.h"
#include <QShowEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QLabel>
#include <QComboBox>
#include <QLocale>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QUrl>
#include <QDesktopServices>

static const int BORDER_WIDTH = 2;
static const int CORNER_RADIUS = 10;

namespace {
bool looksBrokenTranslation(const QString &text) {
    if (text.isEmpty()) {
        return true;
    }

    int suspiciousCount = 0;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('?') || ch == QChar::ReplacementCharacter) {
            ++suspiciousCount;
        }
    }
    return suspiciousCount >= qMax(2, text.size() / 2);
}

QString uiText(const char *source, const QString &zhFallback) {
    const QString translated = QObject::tr(source);
    const QLocale locale = QLocale::system();
    if (translated == QLatin1String(source) || looksBrokenTranslation(translated)) {
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

            QFrame#generalCard {
                background-color: #1E232B;
                border: 1px solid #2B3440;
                border-radius: 8px;
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

        QFrame#generalCard {
            background-color: #FFFFFF;
            border: 1px solid #E5E5E5;
            border-radius: 8px;
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
    )");
}

MPasteSettingsWidget::MPasteSettingsWidget(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MPasteSettingsWidget)
{
    ui->setupUi(this);

    // Frameless + translucent for custom-painted gradient border
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &MPasteSettingsWidget::applyTheme);

    setWindowTitle(uiText("Settings", QStringLiteral("设置")));
    ui->titleLabel->setText(uiText("Settings", QStringLiteral("设置")));
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
        grid->addWidget(pasteShortcutLabel_, 11, 0);
        grid->addWidget(pasteShortcutCombo_, 11, 1, Qt::AlignRight | Qt::AlignVCenter);

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
        grid->addWidget(syncSep, 12, 0, 1, 2);

        syncLabel_ = new QLabel(uiText("Sync folder", QStringLiteral("同步目录")), this);
        syncLabel_->setMinimumHeight(44);
        syncPathEdit_ = new QLineEdit(this);
        syncPathEdit_->setReadOnly(true);
        syncPathEdit_->setMinimumHeight(32);
        syncPathEdit_->setPlaceholderText(uiText("Select a folder to sync", QStringLiteral("选择同步目录")));
        syncPathEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        grid->addWidget(syncLabel_, 13, 0);
        grid->addWidget(syncPathEdit_, 13, 1);

        auto *syncButtonsRow = new QWidget(this);
        auto *syncButtonsLayout = new QHBoxLayout(syncButtonsRow);
        syncButtonsLayout->setContentsMargins(0, 0, 0, 0);
        syncButtonsLayout->setSpacing(6);
        syncButtonsLayout->addStretch(1);

        syncOpenButton_ = new QPushButton(uiText("Open", QStringLiteral("打开")), this);
        syncOpenButton_->setMinimumSize(QSize(52, 32));
        syncOpenButton_->setMaximumHeight(32);
        syncButtonsLayout->addWidget(syncOpenButton_);

        syncChangeButton_ = new QPushButton(uiText("Change", QStringLiteral("修改")), this);
        syncChangeButton_->setMinimumSize(QSize(64, 32));
        syncChangeButton_->setMaximumHeight(32);
        syncButtonsLayout->addWidget(syncChangeButton_);

        grid->addWidget(new QWidget(this), 14, 0);
        grid->addWidget(syncButtonsRow, 14, 1, Qt::AlignRight | Qt::AlignVCenter);

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

    // Card shadow
    auto *shadow = new QGraphicsDropShadowEffect(ui->generalCard);
    shadow->setBlurRadius(16);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(0, 0, 0, 25));
    ui->generalCard->setGraphicsEffect(shadow);

    loadSettings();
}

MPasteSettingsWidget::~MPasteSettingsWidget()
{
    delete ui;
}

void MPasteSettingsWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);

    QConicalGradient grad(r.center(), 135);
    grad.setColorAt(0.00, QColor("#4A90E2"));  // blue
    grad.setColorAt(0.25, QColor("#1abc9c"));  // teal
    grad.setColorAt(0.50, QColor("#fc9867"));  // orange
    grad.setColorAt(0.75, QColor("#9B59B6"));  // purple
    grad.setColorAt(1.00, QColor("#4A90E2"));  // blue (loop)

    QPen pen(QBrush(grad), BORDER_WIDTH);
    p.setPen(pen);
    p.setBrush(darkTheme_ ? QColor("#171B22") : QColor("#F3F3F3"));
    p.drawRoundedRect(r.adjusted(BORDER_WIDTH / 2.0, BORDER_WIDTH / 2.0,
                                 -BORDER_WIDTH / 2.0, -BORDER_WIDTH / 2.0),
                      CORNER_RADIUS, CORNER_RADIUS);
}

void MPasteSettingsWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragPos_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void MPasteSettingsWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - dragPos_);
        event->accept();
    }
}

void MPasteSettingsWidget::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    loadSettings();
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
    applyTheme(ThemeManager::instance()->isDark());

#ifdef Q_OS_WIN
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    autoStartSwitch_->setChecked(reg.contains("MPaste"));
#else
    autoStartSwitch_->setChecked(false);
#endif
}

void MPasteSettingsWidget::accept()
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

#ifdef Q_OS_WIN
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    if (autoStartSwitch_->isChecked()) {
        reg.setValue("MPaste", QDir::toNativeSeparators(qApp->applicationFilePath()));
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
    QDialog::accept();
}

void MPasteSettingsWidget::applyTheme(bool dark) {
    darkTheme_ = dark;
    setStyleSheet(settingsStyleSheet(darkTheme_));
    update();
}
