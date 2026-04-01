// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 AboutWidget 的实现逻辑。
// pos: widget 层中的 AboutWidget 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#include "AboutWidget.h"
#include "ui_AboutWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "WindowBlurHelper.h"
#include "utils/ThemeManager.h"

AboutWidget::AboutWidget(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutWidget)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    setModal(false);
    setAttribute(Qt::WA_TranslucentBackground);

    ui->setupUi(this);
#ifdef MPASTE_VERSION
    ui->label_2->setText(QStringLiteral("MPaste V%1").arg(QStringLiteral(MPASTE_VERSION)));
#endif

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &AboutWidget::applyTheme);
}

AboutWidget::~AboutWidget()
{
    delete ui;
}

void AboutWidget::applyTheme(bool dark) {
    darkTheme_ = dark;
    WindowBlurHelper::enableBlurBehind(this, darkTheme_);

    const QString text = dark ? QStringLiteral("#E6EDF5") : QStringLiteral("#1E2936");
    const QString link = QStringLiteral("#4A90E2");
    const QString sub = dark ? QStringLiteral("#9AA7B5") : QStringLiteral("#5E7084");

    setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background: transparent; }"
        "QLabel#label_2 { font-weight: bold; font-size: 16px; }"
        "QLabel#label_9, QLabel#label_10 { color: %2; font-size: 9pt; }"
    ).arg(text, sub));

    // Re-apply link colors
    for (QLabel *lbl : {ui->label_6, ui->label_8, ui->label_9, ui->label_10}) {
        if (lbl) {
            lbl->setStyleSheet(QStringLiteral("QLabel a { color: %1; }").arg(link));
        }
    }
    update();
}

void AboutWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = 8.0;
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    if (darkTheme_) {
        p.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
    } else {
        p.setPen(QPen(QColor(0, 0, 0, 25), 1.0));
    }
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, radius, radius);
}

void AboutWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragPos_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void AboutWidget::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - dragPos_);
        event->accept();
    }
}
