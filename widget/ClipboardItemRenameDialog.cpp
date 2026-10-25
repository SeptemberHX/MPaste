// input: Depends on ClipboardItemRenameDialog.h, ThemeManager, and Qt layout/widgets.
// output: Themed rename dialog with an inset native frosted surface and opaque fallback.
// pos: Widget-layer rename dialog implementation.
// update: If I change, update this header block and my folder README.md.
// note: Rename dialog now explicitly activates and focuses the input field.
#include "ClipboardItemRenameDialog.h"
#include "WindowBlurHelper.h"
#include <QPainter>
#include "ClipboardCardDelegate.h"
#include "AuroraChrome.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"
#include "BoardInternalHelpers.h"

namespace {
QString fallbackText(const QString &translated, const QString &source, const QString &zhFallback) {
    const QLocale locale = QLocale::system();
    if (translated == source || BoardHelpers::looksBrokenTranslation(translated)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return zhFallback;
        }
        return source;
    }
    return translated;
}

}

ClipboardItemRenameDialog::ClipboardItemRenameDialog(const QString &currentAlias, QWidget *parent)
    : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedWidth(420);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);

    card_ = new QFrame(this);
    card_->setObjectName(QStringLiteral("renameCard"));
    rootLayout->addWidget(card_);

    auto *cardLayout = new QVBoxLayout(card_);
    cardLayout->setContentsMargins(20, 18, 20, 18);
    cardLayout->setSpacing(14);

    const QString aliasText = fallbackText(tr("Alias"), QStringLiteral("Alias"), QStringLiteral("别名"));
    const QString titleText = fallbackText(tr("Name this item"), QStringLiteral("Name this item"),
                                           QStringLiteral("给这条起个名字"));
    const QString customNameText = fallbackText(tr("Alias name"), QStringLiteral("Alias name"), QStringLiteral("别名名称"));
    const QString hintText = fallbackText(tr("Leave empty to clear alias."),
                                          QStringLiteral("Leave empty to clear alias."),
                                          QStringLiteral("留空可清除别名"));
    const QString cancelText = fallbackText(tr("Cancel"), QStringLiteral("Cancel"), QStringLiteral("取消"));
    const QString saveText = fallbackText(tr("Save"), QStringLiteral("Save"), QStringLiteral("保存"));

    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    iconLabel_ = new QLabel(card_);
    iconLabel_->setFixedSize(24, 24);
    iconLabel_->hide();
    titleRow->addWidget(iconLabel_);
    titleLabel_ = new QLabel(titleText, card_);
    titleLabel_->setObjectName(QStringLiteral("auroraTitle"));
    titleRow->addWidget(titleLabel_, 1);
    cardLayout->addLayout(titleRow);

    input_ = new QLineEdit(card_);
    input_->setObjectName(QStringLiteral("auroraInput"));
    input_->setText(currentAlias);
    input_->setPlaceholderText(customNameText);
    input_->setMaxLength(80);
    input_->setFocusPolicy(Qt::StrongFocus);
    cardLayout->addWidget(input_);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    hintLabel_ = new QLabel(hintText, card_);
    hintLabel_->setObjectName(QStringLiteral("auroraHint"));
    buttonRow->addWidget(hintLabel_, 1);

    cancelButton_ = new QPushButton(cancelText, card_);
    cancelButton_->setObjectName(QStringLiteral("auroraSecondary"));
    cancelButton_->setCursor(Qt::PointingHandCursor);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(cancelButton_);

    okButton_ = new QPushButton(saveText, card_);
    okButton_->setObjectName(QStringLiteral("auroraPrimary"));
    okButton_->setCursor(Qt::PointingHandCursor);
    okButton_->setDefault(true);
    connect(okButton_, &QPushButton::clicked, this, &QDialog::accept);
    buttonRow->addWidget(okButton_);

    cardLayout->addLayout(buttonRow);

    setWindowTitle(aliasText);
    applyTheme(ThemeManager::instance()->isDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ClipboardItemRenameDialog::applyTheme);
}

QString ClipboardItemRenameDialog::alias() const {
    return input_ ? input_->text().trimmed() : QString();
}

void ClipboardItemRenameDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        raise();
        activateWindow();
        if (input_) {
            input_->setFocus(Qt::ActiveWindowFocusReason);
            input_->selectAll();
        }
    });
}

void ClipboardItemRenameDialog::setSource(const QPixmap &icon, const QString &appName) {
    accent_ = ClipboardCardDelegate::appAccentColor(icon);
    iconLabel_->setPixmap(ClipboardCardDelegate::appIconTile(icon, appName.isEmpty() ? QStringLiteral("M") : appName,
                                                             24, devicePixelRatioF()));
    iconLabel_->show();
    update();
}

void ClipboardItemRenameDialog::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    // The window keeps a 10 px margin for the blur; the surface is the card inside it.
    AuroraChrome::paintWindow(painter, QRectF(rect()).adjusted(10, 10, -10, -10), dark_, accent_);
}

void ClipboardItemRenameDialog::applyTheme(bool dark) {
    dark_ = dark;
    setProperty("mpasteBlurRadius", AuroraChrome::kRadius);
    setProperty("mpasteBlurInset", 10.0);
    WindowBlurHelper::enableBlurBehind(this, dark);
    setStyleSheet(AuroraChrome::styleSheet(dark) + QStringLiteral("QFrame#renameCard { background: transparent; border: none; }"));
    update();
}
