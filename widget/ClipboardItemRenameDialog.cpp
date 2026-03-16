// input: Depends on ClipboardItemRenameDialog.h, ThemeManager, and Qt layout/widgets.
// output: Implements a themed rename dialog matching the app's card language.
// pos: Widget-layer rename dialog implementation.
// update: If I change, update this header block and my folder README.md.
#include "ClipboardItemRenameDialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"

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

QString fallbackText(const QString &translated, const QString &source, const QString &zhFallback) {
    const QLocale locale = QLocale::system();
    if (translated == source || looksBrokenTranslation(translated)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return zhFallback;
        }
        return source;
    }
    return translated;
}

QString renameDialogStyleSheet(bool dark) {
    if (dark) {
        return QStringLiteral(R"(
            QDialog {
                background: transparent;
            }
            QFrame#renameCard {
                background: rgba(24, 29, 36, 0.96);
                border: 1px solid rgba(255, 255, 255, 0.08);
                border-radius: 12px;
            }
            QLabel#renameTitle {
                color: #F2F6FB;
                font-size: 14px;
                font-weight: 600;
            }
            QLabel#renameHint {
                color: #93A2B3;
                font-size: 11px;
            }
            QLineEdit#renameInput {
                background: rgba(24, 29, 36, 0.95);
                border: 1px solid #3A4655;
                border-radius: 8px;
                color: #F2F6FB;
                padding: 6px 10px;
            }
            QLineEdit#renameInput:focus {
                border-color: #4A90E2;
            }
            QPushButton#renameOk {
                background: #2D7FD3;
                color: #FFFFFF;
                border: none;
                border-radius: 8px;
                padding: 6px 16px;
            }
            QPushButton#renameOk:hover {
                background: #328BE6;
            }
            QPushButton#renameCancel {
                background: transparent;
                color: #93A2B3;
                border: 1px solid #3A4655;
                border-radius: 8px;
                padding: 6px 16px;
            }
            QPushButton#renameCancel:hover {
                color: #F2F6FB;
                border-color: #4A90E2;
            }
        )");
    }

    return QStringLiteral(R"(
        QDialog {
            background: transparent;
        }
        QFrame#renameCard {
            background: rgba(255, 255, 255, 0.98);
            border: 1px solid rgba(0, 0, 0, 0.12);
            border-radius: 12px;
        }
        QLabel#renameTitle {
            color: #2C3E50;
            font-size: 14px;
            font-weight: 600;
        }
        QLabel#renameHint {
            color: #556270;
            font-size: 11px;
        }
        QLineEdit#renameInput {
            background: rgba(255, 255, 255, 0.98);
            border: 1px solid #D6D6D6;
            border-radius: 8px;
            color: #2C3E50;
            padding: 6px 10px;
        }
        QLineEdit#renameInput:focus {
            border-color: #4A90E2;
        }
        QPushButton#renameOk {
            background: #4A90E2;
            color: #FFFFFF;
            border: none;
            border-radius: 8px;
            padding: 6px 16px;
        }
        QPushButton#renameOk:hover {
            background: #5AA0F0;
        }
        QPushButton#renameCancel {
            background: transparent;
            color: #556270;
            border: 1px solid #D6D6D6;
            border-radius: 8px;
            padding: 6px 16px;
        }
        QPushButton#renameCancel:hover {
            color: #2C3E50;
            border-color: #4A90E2;
        }
    )");
}
}

ClipboardItemRenameDialog::ClipboardItemRenameDialog(const QString &currentAlias, QWidget *parent)
    : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedWidth(360);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);

    card_ = new QFrame(this);
    card_->setObjectName(QStringLiteral("renameCard"));
    rootLayout->addWidget(card_);

    auto *cardLayout = new QVBoxLayout(card_);
    cardLayout->setContentsMargins(16, 14, 16, 16);
    cardLayout->setSpacing(12);

    const QString aliasText = fallbackText(tr("Alias"), QStringLiteral("Alias"), QStringLiteral("别名"));
    const QString customNameText = fallbackText(tr("Alias name"), QStringLiteral("Alias name"), QStringLiteral("别名名称"));
    const QString hintText = fallbackText(tr("Leave empty to clear alias."),
                                          QStringLiteral("Leave empty to clear alias."),
                                          QStringLiteral("留空可清除别名。"));
    const QString cancelText = fallbackText(tr("Cancel"), QStringLiteral("Cancel"), QStringLiteral("取消"));
    const QString saveText = fallbackText(tr("Save"), QStringLiteral("Save"), QStringLiteral("保存"));

    titleLabel_ = new QLabel(aliasText, card_);
    titleLabel_->setObjectName(QStringLiteral("renameTitle"));
    cardLayout->addWidget(titleLabel_);

    input_ = new QLineEdit(card_);
    input_->setObjectName(QStringLiteral("renameInput"));
    input_->setText(currentAlias);
    input_->setPlaceholderText(customNameText);
    input_->setMaxLength(80);
    {
        QFont emojiFont = input_->font();
        emojiFont.setFamilies({
            QStringLiteral("Segoe UI Emoji"),
            QStringLiteral("Segoe UI Symbol"),
            QStringLiteral("Segoe UI"),
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Microsoft YaHei"),
            QStringLiteral("Noto Color Emoji"),
            QStringLiteral("Noto Emoji")
        });
        input_->setFont(emojiFont);
    }
    cardLayout->addWidget(input_);

    hintLabel_ = new QLabel(hintText, card_);
    hintLabel_->setObjectName(QStringLiteral("renameHint"));
    cardLayout->addWidget(hintLabel_);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(10);
    buttonRow->addStretch(1);

    cancelButton_ = new QPushButton(cancelText, card_);
    cancelButton_->setObjectName(QStringLiteral("renameCancel"));
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(cancelButton_);

    okButton_ = new QPushButton(saveText, card_);
    okButton_->setObjectName(QStringLiteral("renameOk"));
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
    if (input_) {
        input_->setFocus();
        input_->selectAll();
    }
}

void ClipboardItemRenameDialog::applyTheme(bool dark) {
    setStyleSheet(renameDialogStyleSheet(dark));
}
