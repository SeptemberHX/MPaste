// input: 依赖对应头文件、Qt Widgets/布局、设置对象与自定义开关组件。
// output: 提供设置界面及单组快捷键编辑、GNOME 说明和注册失败回退。
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
#include <QRegularExpression>
#include <QShowEvent>
#include <QMouseEvent>
#include <QWindow>
#include <QPainter>
#include <QPainterPath>
#include <QGridLayout>
#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QSignalBlocker>
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
#include <QIcon>
#include "AuroraChrome.h"

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

// The Aurora look for the settings pages, on top of AuroraChrome's shared rules.
static QString settingsAuroraStyle(bool dark) {
    const QString ink = dark ? QStringLiteral("#ECEDF2") : QStringLiteral("#1C1E25");
    const QString soft = dark ? QStringLiteral("rgba(255,255,255,165)") : QStringLiteral("rgba(28,30,37,165)");
    const QString faint = dark ? QStringLiteral("rgba(255,255,255,115)") : QStringLiteral("rgba(28,30,37,125)");
    const QString fill = dark ? QStringLiteral("rgba(255,255,255,18)") : QStringLiteral("rgba(0,0,0,13)");
    const QString fillHover = dark ? QStringLiteral("rgba(255,255,255,28)") : QStringLiteral("rgba(0,0,0,20)");
    const QString chosen = dark ? QStringLiteral("rgba(255,255,255,26)") : QStringLiteral("rgba(255,255,255,235)");
    const QString panel = dark ? QStringLiteral("rgba(255,255,255,9)") : QStringLiteral("rgba(255,255,255,170)");
    const QString edge = dark ? QStringLiteral("rgba(255,255,255,16)") : QStringLiteral("rgba(0,0,0,16)");
    return QStringLiteral(
        "QWidget#generalCard, QFrame#generalCard, QStackedWidget#settingsStack, QStackedWidget#settingsStack > QWidget"
        "  { background: transparent; border: none; }"
        "QLabel { color: %1; font-size: 13.5px; background: transparent; }"
        "QLabel#section { color: %3; font-size: 12px; font-weight: 500; padding-left: 4px; background: transparent; }"
        "QFrame#card { background: %7; border: 1px solid %8; border-radius: 16px; }"
        // Every box property is set: a native style would otherwise size the tabs itself.
        "QToolButton#navBtn { min-height: 40px; max-height: 40px; margin: 0px; border: none; border-radius: 19px;"
        "  padding: 0 14px; text-align: left; background: transparent; color: %2; font-size: 14px; }"
        "QToolButton#navBtn:hover:!checked { background: %4; color: %1; }"
        "QToolButton#navBtn:checked { background: %6; color: %1; font-weight: 600; }"
        "QComboBox, QLineEdit, QSpinBox, QKeySequenceEdit { min-height: 32px; max-height: 32px; padding: 0 12px;"
        "  border: 1px solid %8; border-radius: 15px; background: %4; color: %1; }"
        "QComboBox:hover, QLineEdit:hover, QSpinBox:hover { background: %5; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QPushButton { min-height: 30px; padding: 0 14px; border: none; border-radius: 15px; background: %4; color: %1; }"
        "QPushButton:hover { background: %5; }"
        "QToolButton#cardStyleChoice { border: 1px solid %8; border-radius: 14px; background: %7; color: %1;"
        "  padding: 10px; font-size: 12.5px; }"
        "QToolButton#cardStyleChoice:checked { border: 1.5px solid #5B8DEF; background: %6; }"
        "QToolButton#cardStyleChoice:hover:!checked { background: %5; }")
        .arg(ink, soft, faint, fill, fillHover, chosen, panel, edge);
}

QWidget *MPasteSettingsWidget::makeSegmentFor(QComboBox *combo, QWidget *parent) {
    auto *segment = new QWidget(parent);
    segment->setObjectName(QStringLiteral("auroraSegment"));
    segment->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QHBoxLayout(segment);
    layout->setContentsMargins(3, 2, 3, 2);
    layout->setSpacing(2);
    auto *group = new QButtonGroup(segment);
    group->setExclusive(true);
    for (int i = 0; i < combo->count(); ++i) {
        auto *button = new QPushButton(combo->itemText(i), segment);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        group->addButton(button, i);
        layout->addWidget(button);
    }
    if (auto *current = group->button(combo->currentIndex())) {
        current->setChecked(true);
    }
    connect(group, &QButtonGroup::idClicked, combo, &QComboBox::setCurrentIndex);
    connect(combo, &QComboBox::currentIndexChanged, segment, [group](int index) {
        if (auto *button = group->button(index)) {
            button->setChecked(true);
        }
    });
    return segment;
}

QWidget *MPasteSettingsWidget::makeCardStyleChoices(QWidget *parent) {
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto *group = new QButtonGroup(row);
    group->setExclusive(true);
    const qreal dpr = devicePixelRatioF();
    // A miniature of each style: Still a neutral card, Aurora a coloured corner light.
    auto miniature = [dpr](bool aurora) {
        const QSize size(220, 72);
        QPixmap pixmap(size * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath card;
        card.addRoundedRect(QRectF(0.5, 0.5, size.width() - 1, size.height() - 1), 10, 10);
        p.fillPath(card, QColor(aurora ? QStringLiteral("#17181E") : QStringLiteral("#18191D")));
        if (aurora) {
            QRadialGradient light(QPointF(0, 0), size.width() * 0.9);
            light.setColorAt(0.0, QColor(7, 193, 96, 110));
            light.setColorAt(0.6, QColor(7, 193, 96, 0));
            p.save();
            p.setClipPath(card);
            p.fillRect(QRectF(QPointF(0, 0), QSizeF(size)), light);
            p.restore();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#07C160")));
            p.drawRoundedRect(QRectF(12, 12, 14, 14), 4, 4);
            p.setBrush(QColor(255, 255, 255, 110));
            p.drawRoundedRect(QRectF(32, 16, 80, 6), 3, 3);
            p.setBrush(QColor(255, 255, 255, 55));
            p.drawRoundedRect(QRectF(12, 40, 150, 5), 2.5, 2.5);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 95));
            p.drawRoundedRect(QRectF(12, 14, 150, 6), 3, 3);
            p.setBrush(QColor(255, 255, 255, 50));
            p.drawRoundedRect(QRectF(12, 28, 110, 5), 2.5, 2.5);
            p.setBrush(QColor(QStringLiteral("#07C160")));
            p.drawRoundedRect(QRectF(12, 52, 10, 10), 3, 3);
            p.setBrush(QColor(255, 255, 255, 60));
            p.drawRoundedRect(QRectF(27, 55, 60, 4), 2, 2);
        }
        p.setPen(QPen(QColor(255, 255, 255, 22), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(card);
        return pixmap;
    };
    const struct { const char *name; QString zhName; const char *note; QString zhNote; bool aurora; } styles[] = {
        {"Still", QStringLiteral("静物"), "A neutral card, the content first", QStringLiteral("中性卡片，内容优先"), false},
        {"Aurora", QStringLiteral("流光"), "The app's colour as a corner light", QStringLiteral("应用色化作角落的光"), true},
    };
    for (int i = 0; i < 2; ++i) {
        auto *button = new QToolButton(row);
        button->setObjectName(QStringLiteral("cardStyleChoice"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(QIcon(miniature(styles[i].aurora)));
        button->setIconSize(QSize(220, 72));
        button->setText(uiText(styles[i].name, styles[i].zhName) + QStringLiteral("\n")
                        + uiText(styles[i].note, styles[i].zhNote));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        group->addButton(button, cardStyleCombo_->findData(styles[i].aurora ? MPasteSettings::CardAurora : MPasteSettings::CardStill));
        layout->addWidget(button, 1);
    }
    if (auto *current = group->button(cardStyleCombo_->currentIndex())) {
        current->setChecked(true);
    }
    connect(group, &QButtonGroup::idClicked, cardStyleCombo_, &QComboBox::setCurrentIndex);
    connect(cardStyleCombo_, &QComboBox::currentIndexChanged, row, [group](int index) {
        if (auto *button = group->button(index)) {
            button->setChecked(true);
        }
    });
    return row;
}

MPasteSettingsWidget::MPasteSettingsWidget(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MPasteSettingsWidget)
{
    ui->setupUi(this);

    setMinimumWidth(784);
    setMaximumWidth(784);
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
        titleLabel->setObjectName(QStringLiteral("glassSettingsTitle"));
        QFont f = titleLabel->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() + 0.5);
        titleLabel->setFont(f);
        titleLabel->hide();   // the sidebar carries the title
        tb->addStretch();
        auto *closeBtn = new QToolButton(titleBar);
        closeBtn->setText(QStringLiteral("\u00D7"));
        closeBtn->setFocusPolicy(Qt::NoFocus);
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setText(QStringLiteral("\u2715"));
        closeBtn->setFixedSize(32, 32);
        closeBtn->setObjectName(QStringLiteral("auroraClose"));
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

        cardStyleLabel_ = new QLabel(uiText("Card style", QStringLiteral("卡片风格")), this);
        cardStyleLabel_->setMinimumHeight(44);
        cardStyleCombo_ = new QComboBox(this);
        cardStyleCombo_->setMinimumSize(QSize(140, 32));
        cardStyleCombo_->addItem(uiText("Still", QStringLiteral("静物")), static_cast<int>(MPasteSettings::CardStill));
        cardStyleCombo_->addItem(uiText("Aurora", QStringLiteral("流光")), static_cast<int>(MPasteSettings::CardAurora));
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

        activationLayoutLabel_ = new QLabel(uiText("Opens", QStringLiteral("唤出形态")), this);
        activationLayoutLabel_->setMinimumHeight(44);
        activationLayoutCombo_ = new QComboBox(this);
        activationLayoutCombo_->setMinimumSize(QSize(140, 32));
        activationLayoutCombo_->setMaximumHeight(32);
        activationLayoutCombo_->addItem(uiText("Bottom panel", QStringLiteral("底部面板")), static_cast<int>(MPasteSettings::LayoutPanel));
        activationLayoutCombo_->addItem(uiText("Stack by the caret", QStringLiteral("光标旁堆栈")), static_cast<int>(MPasteSettings::LayoutStack));

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
        cardStyleCombo_->setMinimumHeight(36);
        cardStyleCombo_->setMaximumHeight(36);
        pasteShortcutCombo_->setMinimumHeight(36);
        pasteShortcutCombo_->setMaximumHeight(36);
        activationLayoutCombo_->setMinimumHeight(36);
        activationLayoutCombo_->setMaximumHeight(36);
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
        sideWidget->setFixedWidth(208);
        auto *sideLayout = new QVBoxLayout(sideWidget);
        sideLayout->setContentsMargins(14, 2, 14, 14);
        sideLayout->setSpacing(6);
        {
            auto *brand = new QHBoxLayout;
            // The mark lines up with the tab icons (the tabs' 14 px padding).
            brand->setContentsMargins(14, 0, 0, 20);
            brand->setSpacing(10);
            auto *mark = new QLabel(sideWidget);
            mark->setPixmap(QIcon(QStringLiteral(":/resources/resources/mpaste.svg")).pixmap(QSize(28, 28), devicePixelRatioF()));
            brand->addWidget(mark);
            auto *brandTitle = new QLabel(uiText("MPaste Settings", QStringLiteral("MPaste 设置")), sideWidget);
            brandTitle->setObjectName(QStringLiteral("auroraTitle"));
            brand->addWidget(brandTitle, 1);
            sideLayout->addLayout(brand);
        }

        auto *navGroup = new QButtonGroup(this);
        navGroup->setExclusive(true);

        auto makeNavBtn = [&](const QString &iconName, const QString &text, int id) {
            auto *btn = new QToolButton();
            btn->setObjectName(QStringLiteral("navBtn"));
            btn->setIcon(IconResolver::themedIcon(iconName, ThemeManager::instance()->isDark()));
            btn->setIconSize(QSize(18, 18));
            btn->setText(text);
            btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            btn->setCheckable(true);
            btn->setFixedSize(180, 40);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFocusPolicy(Qt::NoFocus);
            navGroup->addButton(btn, id);
            sideLayout->addWidget(btn, 0, Qt::AlignHCenter);
        };

        makeNavBtn(QStringLiteral("settings"), uiText("General", QStringLiteral("通用")), 0);
        makeNavBtn(QStringLiteral("rename"), uiText("Shortcuts", QStringLiteral("快捷键")), 1);
        makeNavBtn(QStringLiteral("save_black"), uiText("Sync", QStringLiteral("同步")), 2);
        makeNavBtn(QStringLiteral("shield"), uiText("Privacy", QStringLiteral("隐私")), 3);
        makeNavBtn(QStringLiteral("text_plain"), uiText("Text recognition", QStringLiteral("文字识别")), 4);
        sideLayout->addStretch();
        makeNavBtn(QStringLiteral("info"), uiText("About", QStringLiteral("关于")), 5);

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
            // Theme as a segmented control and card style as two pictured choices;
            // both drive the (hidden) combo boxes the settings load and save through.
            g->addWidget(themeLabel_, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(makeSegmentFor(themeCombo_, generalPage), 0, 1, Qt::AlignRight | Qt::AlignVCenter);
            themeCombo_->hide();
            g->addWidget(cardStyleLabel_, 1, 0, 1, 2, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(makeCardStyleChoices(generalPage), 2, 0, 1, 2);
            cardStyleCombo_->hide();
            g->addWidget(ui->label_autostart, 3, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(autoStartSwitch_, 3, 1, Qt::AlignRight | Qt::AlignVCenter);
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
            g->addWidget(activationLayoutLabel_, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(activationLayoutCombo_, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
            g->addWidget(pasteShortcutLabel_, 2, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(pasteShortcutCombo_, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
            sLayout->addWidget(makeCard(g));
        }

#ifdef Q_OS_LINUX
        if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
            auto *help = new QLabel(QStringLiteral(
                "GNOME Wayland：快捷键由桌面注册。扩展尚未加载时，使用系统的 MPaste 自定义快捷键唤起窗口。"
                "可在「设置 → 键盘 → 自定义快捷键」查看或排查冲突。\n"
                "后台采集和自动粘贴仍需要启用 MPaste 扩展；首次安装后请注销再登录。"), shortcutsPage);
            help->setWordWrap(true);
            sLayout->addWidget(help);
        }
#endif
        ui->shortcutEdit->setMaximumSequenceLength(1);
        sLayout->addStretch();
        stack->addWidget(shortcutsPage);

        // ── Sync page ──
        auto *syncPage = new QWidget(stack);
        auto *syncPageLayout = createPageLayout(syncPage);
        syncPageLayout->addWidget(makeSectionLabel(uiText("Sync", QStringLiteral("同步")), syncPage));
        {
            auto *g = makeSettingsGrid();
            g->addWidget(syncLabel_, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(syncPathEdit_, 0, 1);
            g->addWidget(syncButtonsRow, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
            syncPageLayout->addWidget(makeCard(g));
        }
        auto *syncHelp = new QLabel(uiText("Point this at a folder a sync tool keeps (Syncthing, a cloud drive) to share history between machines.",
                                           QStringLiteral("把它指向同步工具管理的文件夹（Syncthing、网盘等），即可在多台电脑间共享历史。")), syncPage);
        syncHelp->setObjectName(QStringLiteral("auroraHint"));
        syncHelp->setWordWrap(true);
        syncPageLayout->addWidget(syncHelp);
        syncPageLayout->addStretch();
        stack->addWidget(syncPage);

        // ── Privacy page ──
        auto *privacyPage = new QWidget(stack);
        auto *pLayout = createPageLayout(privacyPage);
        pLayout->addWidget(makeSectionLabel(uiText("Privacy", QStringLiteral("隐私")), privacyPage));
        {
            auto *g = makeSettingsGrid();
            auto *sensitiveLabel = new QLabel(uiText("Skip what password managers mark as secret",
                                                     QStringLiteral("不记录密码管理器标记为敏感的内容")), privacyPage);
            sensitiveLabel->setMinimumHeight(44);
            sensitiveSwitch_ = new ToggleSwitch(privacyPage);
            g->addWidget(sensitiveLabel, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(sensitiveSwitch_, 0, 1, Qt::AlignRight | Qt::AlignVCenter);

            auto *appsLabel = new QLabel(uiText("Never record from", QStringLiteral("不记录这些应用")), privacyPage);
            appsLabel->setMinimumHeight(24);
            ignoredAppsEdit_ = new QLineEdit(privacyPage);
            ignoredAppsEdit_->setMinimumHeight(36);
            ignoredAppsEdit_->setPlaceholderText(uiText("App names, separated by commas", QStringLiteral("应用名，用逗号分隔")));
            ignoredAppsEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            g->addWidget(appsLabel, 1, 0, 1, 2, Qt::AlignLeft | Qt::AlignVCenter);
            g->addWidget(ignoredAppsEdit_, 2, 0, 1, 2);
            pLayout->addWidget(makeCard(g));
        }
        auto *privacyHelp = new QLabel(uiText(
            "Password managers such as KeePassXC mark the passwords they copy; those copies are left out of history. "
            "Copies from the apps listed are left out entirely; use the name a card shows as its source. "
            "Items already in history stay until you delete them.",
            QStringLiteral("KeePassXC 等密码管理器复制密码时会附带“敏感”标记，带标记的内容不会被记录。"
                           "列表里的应用复制的任何内容都不会被记录，名称与卡片上显示的来源应用一致。"
                           "已经记录下来的条目不受影响，需要手动删除。")), privacyPage);
        privacyHelp->setObjectName(QStringLiteral("auroraHint"));
        privacyHelp->setWordWrap(true);
        pLayout->addWidget(privacyHelp);
        pLayout->addStretch();
        stack->addWidget(privacyPage);

        // ── Text recognition page ──
        auto *maintenancePage = new QWidget(stack);
        auto *mLayout = createPageLayout(maintenancePage);

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

        mLayout->addWidget(makeSectionLabel(uiText("Text recognition", QStringLiteral("文字识别")), maintenancePage));
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
        aboutLayout->setContentsMargins(20, 20, 20, 20);
        aboutLayout->setSpacing(0);

        aboutLayout->addStretch(1);

        auto *logoLabel = new QLabel(aboutPage);
        logoLabel->setPixmap(QPixmap(QStringLiteral(":/resources/resources/mpaste.svg")).scaled(
            64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        logoLabel->setAlignment(Qt::AlignCenter);
        aboutLayout->addWidget(logoLabel);

        aboutLayout->addSpacing(16);

        auto *nameLabel = new QLabel(QStringLiteral("MPaste V" MPASTE_VERSION), aboutPage);
        nameLabel->setAlignment(Qt::AlignCenter);
        nameLabel->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; background: transparent;"));
        aboutLayout->addWidget(nameLabel);

        aboutLayout->addSpacing(20);

        auto *authorLabel = new QLabel(
            QStringLiteral("Author: SeptemberHX<br>"
                           "Github: <a href=\"https://github.com/SeptemberHX/MPaste\">SeptemberHX/MPaste</a><br>"
                           "Email: <a href=\"mailto:september_hx@outlook.com\">september_hx@outlook.com</a>"),
            aboutPage);
        authorLabel->setAlignment(Qt::AlignCenter);
        authorLabel->setOpenExternalLinks(true);
        authorLabel->setStyleSheet(QStringLiteral("font-size: 13px; background: transparent; line-height: 1.8;"));
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
    if (cardStyleCombo_) connect(cardStyleCombo_, &QComboBox::currentIndexChanged, this, save);
    if (retentionUnitCombo_) connect(retentionUnitCombo_, &QComboBox::currentIndexChanged, this, save);
    if (pasteShortcutCombo_) connect(pasteShortcutCombo_, &QComboBox::currentIndexChanged, this, save);
    if (activationLayoutCombo_) connect(activationLayoutCombo_, &QComboBox::currentIndexChanged, this, save);
    if (ocrBackendCombo_) connect(ocrBackendCombo_, &QComboBox::currentIndexChanged, this, save);
    if (autoOcrSwitch_) connect(autoOcrSwitch_, &ToggleSwitch::toggled, this, save);
    if (sensitiveSwitch_) connect(sensitiveSwitch_, &ToggleSwitch::toggled, this, save);
    if (ignoredAppsEdit_) connect(ignoredAppsEdit_, &QLineEdit::editingFinished, this, save);
    connect(ui->daySpinBox, &QSpinBox::valueChanged, this, save);
    connect(ui->itemScaleSlider, &QSlider::valueChanged, this, save);
    connect(ui->shortcutEdit, &QKeySequenceEdit::editingFinished, this, save);

    adjustSize();
}

MPasteSettingsWidget::~MPasteSettingsWidget()
{
    delete ui;
}

void MPasteSettingsWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    AuroraChrome::paintWindow(p, QRectF(rect()), darkTheme_, QColor(QStringLiteral("#5B8DEF")));
}

void MPasteSettingsWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Only drag from the top 42px (title bar area)
        if (event->position().toPoint().y() <= 42) {
            // Wayland ignores move(); the compositor drags the window instead.
            if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))
                    && windowHandle() && windowHandle()->startSystemMove()) {
                event->accept();
                return;
            }
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
    if (activationLayoutCombo_) {
        const int index = activationLayoutCombo_->findData(static_cast<int>(settings->getActivationLayout()));
        activationLayoutCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (pasteShortcutCombo_) {
        const int index = pasteShortcutCombo_->findData(static_cast<int>(settings->getPasteShortcutMode()));
        pasteShortcutCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    ui->itemScaleSlider->setValue(settings->getItemScale());
    ui->scaleValueLabel->setText(QString("%1%").arg(settings->getItemScale()));
    toggleSwitch_->setChecked(settings->isPlaySound());
    if (cardStyleCombo_) {
        const int index = cardStyleCombo_->findData(static_cast<int>(settings->getCardStyle()));
        cardStyleCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
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
    if (sensitiveSwitch_) {
        sensitiveSwitch_->setChecked(settings->isSkipSensitive());
    }
    if (ignoredAppsEdit_) {
        ignoredAppsEdit_->setText(settings->getIgnoredApps().join(QStringLiteral(", ")));
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
        if (settings->getShortcutStr() != newShortcut) {
            const QSignalBlocker blocker(ui->shortcutEdit);
            ui->shortcutEdit->setKeySequence(QKeySequence(settings->getShortcutStr()));
        }
    }

    if (activationLayoutCombo_) {
        settings->setActivationLayout(static_cast<MPasteSettings::ActivationLayout>(activationLayoutCombo_->currentData().toInt()));
    }
    if (pasteShortcutCombo_) {
        settings->setPasteShortcutMode(static_cast<MPasteSettings::PasteShortcutMode>(pasteShortcutCombo_->currentData().toInt()));
    }
    const int newScale = ui->itemScaleSlider->value();
    settings->setItemScale(newScale);
    settings->setPlaySound(toggleSwitch_->isChecked());
    if (cardStyleCombo_) {
        settings->setCardStyle(static_cast<MPasteSettings::CardStyle>(cardStyleCombo_->currentData().toInt()));
    }
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
    if (sensitiveSwitch_) {
        settings->setSkipSensitive(sensitiveSwitch_->isChecked());
    }
    if (ignoredAppsEdit_) {
        // Commas, or the Chinese ones a Chinese input method types.
        static const QRegularExpression separators(QStringLiteral("[,，;；\\n]"));
        settings->setIgnoredApps(ignoredAppsEdit_->text().split(separators, Qt::SkipEmptyParts));
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
    AuroraChrome::attachBackdrop(this, darkTheme_);
    setStyleSheet(settingsStyleSheet(darkTheme_) + AuroraChrome::styleSheet(darkTheme_) + settingsAuroraStyle(darkTheme_));

    // Update sidebar icons for the new theme.
    static const QStringList navIcons = {
        QStringLiteral("settings"), QStringLiteral("rename"), QStringLiteral("save_black"),
        QStringLiteral("shield"), QStringLiteral("text_plain"), QStringLiteral("info")
    };
    const auto navBtns = findChildren<QToolButton *>(QStringLiteral("navBtn"));
    for (int i = 0; i < navBtns.size() && i < navIcons.size(); ++i) {
        navBtns[i]->setIcon(IconResolver::themedIcon(navIcons[i], dark));
    }

    update();
}
