#include "MPasteSettingsWidget.h"
#include "ui_MPasteSettingsWidget.h"
#include "utils/MPasteSettings.h"
#include "ToggleSwitch.h"
#include <QShowEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QDir>
#include <QSettings>

static const int BORDER_WIDTH = 2;
static const int CORNER_RADIUS = 10;

static QString settingsStyleSheet() {
    return QStringLiteral(R"(
        /* ── Dialog — background is drawn in paintEvent ── */
        QDialog {
            background: transparent;
        }

        /* ── Title ── */
        QLabel#titleLabel {
            color: #1A1A1A;
            font-size: 20px;
            font-weight: 700;
            background: transparent;
        }

        /* ── Card ── */
        QFrame#generalCard {
            background-color: #FFFFFF;
            border: 1px solid #E5E5E5;
            border-radius: 8px;
        }

        /* ── Separators ── */
        QFrame#sep1, QFrame#sep2, QFrame#sep_autostart, QFrame#sep3, QFrame#sep4 {
            background-color: #F0F0F0;
            border: none;
            max-height: 1px;
        }

        /* ── Row labels ── */
        QFrame#generalCard QLabel {
            color: #1A1A1A;
            font-size: 13px;
            font-weight: 400;
            background: transparent;
            padding: 0;
            border: none;
        }

        /* ── SpinBox ── */
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

        /* ── QKeySequenceEdit ── */
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

        /* ── Buttons ── */
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

        /* OK — accent */
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

        /* ── Slider ── */
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

    setStyleSheet(settingsStyleSheet());

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

    // Gradient border — conical gradient gives a smooth color loop around the edge
    QConicalGradient grad(r.center(), 135);
    grad.setColorAt(0.00, QColor("#4A90E2"));  // blue
    grad.setColorAt(0.25, QColor("#1abc9c"));  // teal
    grad.setColorAt(0.50, QColor("#fc9867"));  // orange
    grad.setColorAt(0.75, QColor("#9B59B6"));  // purple
    grad.setColorAt(1.00, QColor("#4A90E2"));  // blue (loop)

    QPen pen(QBrush(grad), BORDER_WIDTH);
    p.setPen(pen);
    p.setBrush(QColor("#F3F3F3"));
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
    ui->numSpinBox->setValue(settings->getMaxSize());
    ui->shortcutEdit->setKeySequence(QKeySequence(settings->getShortcutStr()));
    ui->itemScaleSlider->setValue(settings->getItemScale());
    ui->scaleValueLabel->setText(QString("%1%").arg(settings->getItemScale()));
    toggleSwitch_->setChecked(settings->isPlaySound());

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

    settings->setMaxSize(ui->numSpinBox->value());

    QString newShortcut = ui->shortcutEdit->keySequence().toString();
    if (!newShortcut.isEmpty() && newShortcut != settings->getShortcutStr()) {
        settings->setShortcutStr(newShortcut);
        emit shortcutChanged(newShortcut);
    }

    settings->setItemScale(ui->itemScaleSlider->value());
    settings->setPlaySound(toggleSwitch_->isChecked());

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
    QDialog::accept();
}
