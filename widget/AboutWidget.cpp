// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 關於視窗；支援原生磨砂的平台使用半透明圓角底板，否則使用實色底板。
// pos: widget 层中的 AboutWidget 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
#include "SurfacePainter.h"
#include "AboutWidget.h"
#include "ui_AboutWidget.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QWindow>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUrl>
#include <QPushButton>
#include <QLabel>
#include <QIcon>
#include <QHBoxLayout>
#include <QDesktopServices>

#include "WindowBlurHelper.h"
#include "AuroraChrome.h"
#include "BoardInternalHelpers.h"
#include "utils/ThemeManager.h"

AboutWidget::AboutWidget(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutWidget)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    setModal(false);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(400, 340);
    const bool zh = BoardHelpers::prefersChineseUi();

    // Laid out here rather than from AboutWidget.ui: a centred mark, name,
    // version and one line, then the links as pills and the credits small.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 30, 24, 18);
    layout->setSpacing(6);

    auto *mark = new QLabel(this);
    mark->setAlignment(Qt::AlignCenter);
    mark->setPixmap(QIcon(QStringLiteral(":/resources/resources/mpaste.svg")).pixmap(QSize(64, 64), devicePixelRatioF()));
    layout->addWidget(mark);
    layout->addSpacing(6);

    auto *name = new QLabel(QStringLiteral("MPaste"), this);
    name->setObjectName(QStringLiteral("aboutName"));
    name->setAlignment(Qt::AlignCenter);
    layout->addWidget(name);

#ifdef MPASTE_VERSION
    const QString version = QStringLiteral(MPASTE_VERSION);
#else
    const QString version;
#endif
    auto *versionLabel = new QLabel((zh ? QStringLiteral("版本 ") : QStringLiteral("Version ")) + version, this);
    versionLabel->setObjectName(QStringLiteral("auroraSubtitle"));
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);

    auto *tagline = new QLabel(zh ? QStringLiteral("剪贴板历史，随手找回复制过的一切。")
                                  : QStringLiteral("Clipboard history: get back anything you copied."), this);
    tagline->setObjectName(QStringLiteral("aboutTagline"));
    tagline->setAlignment(Qt::AlignCenter);
    tagline->setWordWrap(true);
    layout->addWidget(tagline);
    layout->addStretch(1);

    auto *links = new QHBoxLayout;
    links->setSpacing(8);
    links->addStretch(1);
    auto addLink = [this, links](const QString &text, const QString &url) {
        auto *button = new QPushButton(text, this);
        button->setObjectName(QStringLiteral("auroraSecondary"));
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, [url]() { QDesktopServices::openUrl(QUrl(url)); });
        links->addWidget(button);
    };
    addLink(QStringLiteral("GitHub"), QStringLiteral("https://github.com/SeptemberHX/MPaste"));
    addLink(zh ? QStringLiteral("更新日志") : QStringLiteral("Releases"),
            QStringLiteral("https://github.com/SeptemberHX/MPaste/releases"));
    links->addStretch(1);
    layout->addLayout(links);
    layout->addSpacing(10);

    auto *credits = new QLabel(QStringLiteral(
        "SeptemberHX · september_hx@outlook.com<br>"
        "Icons by Pixel perfect from <a href=\"https://www.flaticon.com\">flaticon.com</a> · "
        "sound from <a href=\"https://www.zapsplat.com\">zapsplat.com</a>"), this);
    credits->setObjectName(QStringLiteral("auroraHint"));
    credits->setAlignment(Qt::AlignCenter);
    credits->setWordWrap(true);
    credits->setOpenExternalLinks(true);
    credits->setTextFormat(Qt::RichText);
    layout->addWidget(credits);

    closeButton_ = new QToolButton(this);
    closeButton_->setObjectName(QStringLiteral("auroraClose"));
    closeButton_->setText(QStringLiteral("✕"));
    closeButton_->setCursor(Qt::PointingHandCursor);
    closeButton_->setFixedSize(32, 32);
    connect(closeButton_, &QToolButton::clicked, this, &QDialog::reject);

    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &AboutWidget::applyTheme);
}

AboutWidget::~AboutWidget()
{
    delete ui;
}

void AboutWidget::applyTheme(bool dark) {
    darkTheme_ = dark;
    AuroraChrome::attachBackdrop(this, darkTheme_);
    setStyleSheet(AuroraChrome::styleSheet(dark) + QStringLiteral(
        "QLabel#aboutName { color: %1; font-size: 20px; font-weight: 700; background: transparent; }"
        "QLabel#aboutTagline { color: %2; font-size: 13px; background: transparent; }"
        "QLabel#auroraHint a { color: %3; }")
        .arg(dark ? QStringLiteral("#ECEDF2") : QStringLiteral("#1C1E25"),
             dark ? QStringLiteral("rgba(255,255,255,175)") : QStringLiteral("rgba(28,30,37,175)"),
             dark ? QStringLiteral("#A9C4FF") : QStringLiteral("#2F68D8")));
    update();
}

void AboutWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    AuroraChrome::paintWindow(p, QRectF(rect()), darkTheme_, QColor(QStringLiteral("#5B8DEF")));
}

void AboutWidget::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    if (closeButton_) {
        closeButton_->move(width() - closeButton_->width() - 10, 10);
    }
}

void AboutWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // Wayland ignores move(); the compositor drags the window instead.
        if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))
                && windowHandle() && windowHandle()->startSystemMove()) {
            dragPos_ = QPoint();
            event->accept();
            return;
        }
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
