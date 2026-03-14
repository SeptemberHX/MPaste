// input: Depends on ThemeManager.h, MPasteSettings, Qt application and style hints.
// output: Applies theme palette and merged QSS to the application.
// pos: utils layer theme service implementation.
// update: If I change, update this header block and utils/README.md.
// note: Qt < 6.5 does not expose QStyleHints::colorScheme/colorSchemeChanged; fall back to palette change signals.
#include "ThemeManager.h"

#include <QFile>
#include <QGuiApplication>
#include <QStyleHints>
#include <QApplication>
#include <QEvent>

#include "MPasteSettings.h"

ThemeManager *ThemeManager::instance() {
    static ThemeManager manager;
    return &manager;
}

ThemeManager::ThemeManager() = default;

void ThemeManager::initialize() {
    if (initialized_) {
        return;
    }
    initialized_ = true;

    auto *settings = MPasteSettings::getInst();
    connect(settings, &MPasteSettings::themeModeChanged, this, [this]() {
        applyTheme();
    });

    if (auto *hints = QGuiApplication::styleHints()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this]() {
            if (MPasteSettings::getInst()->getThemeMode() == MPasteSettings::ThemeSystem) {
                applyTheme();
            }
        });
#else
        Q_UNUSED(hints);
        qApp->installEventFilter(this);
#endif
    }

    applyTheme();
}

bool ThemeManager::eventFilter(QObject *watched, QEvent *event) {
    if (watched == qApp && event && event->type() == QEvent::ApplicationPaletteChange) {
        if (MPasteSettings::getInst()->getThemeMode() == MPasteSettings::ThemeSystem) {
            const bool systemDark = MPasteSettings::getInst()->isDarkTheme();
            if (systemDark != dark_) {
                applyTheme();
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void ThemeManager::applyTheme() {
    const bool dark = MPasteSettings::getInst()->isDarkTheme();
    const QString style = mergedStyleSheet(dark);
    if (!style.isEmpty()) {
        qApp->setStyleSheet(style);
    }
    qApp->setPalette(palette(dark));

    if (dark_ != dark) {
        dark_ = dark;
        emit themeChanged(dark_);
    } else {
        emit themeChanged(dark_);
    }
}

bool ThemeManager::isDark() const {
    return dark_;
}

QPalette ThemeManager::palette(bool dark) const {
    QPalette pal;
    if (!dark) {
        return pal;
    }

    pal.setColor(QPalette::Window, QColor("#151A22"));
    pal.setColor(QPalette::WindowText, QColor("#D6DEE8"));
    pal.setColor(QPalette::Base, QColor("#1E232B"));
    pal.setColor(QPalette::AlternateBase, QColor("#1A1F27"));
    pal.setColor(QPalette::ToolTipBase, QColor("#1E232B"));
    pal.setColor(QPalette::ToolTipText, QColor("#E6EDF5"));
    pal.setColor(QPalette::Text, QColor("#D6DEE8"));
    pal.setColor(QPalette::Button, QColor("#1E232B"));
    pal.setColor(QPalette::ButtonText, QColor("#D6DEE8"));
    pal.setColor(QPalette::BrightText, QColor("#FFFFFF"));
    pal.setColor(QPalette::Highlight, QColor("#2D7FD3"));
    pal.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));

    return pal;
}

QString ThemeManager::mergedStyleSheet(bool dark) const {
    QString style = loadStyleFile(QStringLiteral(":/resources/resources/style/defaultStyle.qss"));
    if (dark) {
        const QString darkStyle = loadStyleFile(QStringLiteral(":/resources/resources/style/darkStyle.qss"));
        if (!darkStyle.isEmpty()) {
            style.append("\n");
            style.append(darkStyle);
        }
    }

    const auto tokens = themeTokens(dark);
    return applyTokens(style, tokens);
}

QString ThemeManager::loadStyleFile(const QString &path) const {
    QFile styleFile(path);
    if (!styleFile.open(QFile::ReadOnly | QFile::Text)) {
        return {};
    }
    QByteArray bytes = styleFile.readAll();
    if (bytes.startsWith(QByteArrayLiteral("\xEF\xBB\xBF"))) {
        bytes.remove(0, 3);
    }
    const QString content = QString::fromUtf8(bytes);
    styleFile.close();
    return content;
}

QString ThemeManager::applyTokens(const QString &style, const QHash<QString, QString> &tokens) const {
    QString result = style;
    QStringList keys = tokens.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
        return a.size() > b.size();
    });
    for (const QString &tokenKey : keys) {
        const QString key = QStringLiteral("{{%1}}").arg(tokenKey);
        result.replace(key, tokens.value(tokenKey));
    }
    return result;
}

QHash<QString, QString> ThemeManager::themeTokens(bool dark) const {
    QHash<QString, QString> tokens;

    if (dark) {
        tokens.insert(QStringLiteral("text_primary"), QStringLiteral("#F2F6FB"));
        tokens.insert(QStringLiteral("text_secondary"), QStringLiteral("#93A2B3"));
        tokens.insert(QStringLiteral("accent_blue"), QStringLiteral("#4A90E2"));
        tokens.insert(QStringLiteral("accent_orange"), QStringLiteral("#fc9867"));
        tokens.insert(QStringLiteral("tab_text"), QStringLiteral("#F2F6FB"));
        tokens.insert(QStringLiteral("tab_checked_bg"), QStringLiteral("rgba(28, 34, 44, 0.92)"));
        tokens.insert(QStringLiteral("type_checked_bg"), QStringLiteral("rgba(28, 34, 44, 0.92)"));
        tokens.insert(QStringLiteral("top_hover_bg"), QStringLiteral("rgba(28, 34, 44, 0.92)"));
        tokens.insert(QStringLiteral("line_edit_border"), QStringLiteral("#3A4655"));
        tokens.insert(QStringLiteral("line_edit_border_focus"), QStringLiteral("#4A90E2"));
        tokens.insert(QStringLiteral("line_edit_bg"), QStringLiteral("rgba(24, 29, 36, 0.95)"));
        tokens.insert(QStringLiteral("badge_bg"), QStringLiteral("rgba(20, 26, 34, 0.55)"));
        tokens.insert(QStringLiteral("badge_text"), QStringLiteral("#F2F6FB"));
        tokens.insert(QStringLiteral("badge_border"), QStringLiteral("rgba(116, 154, 214, 0.25)"));
        tokens.insert(QStringLiteral("badge_bg_hover"), QStringLiteral("rgba(30, 38, 48, 0.7)"));
        tokens.insert(QStringLiteral("badge_border_hover"), QStringLiteral("rgba(116, 154, 214, 0.35)"));
        tokens.insert(QStringLiteral("menu_bg"), QStringLiteral("#1E232B"));
        tokens.insert(QStringLiteral("menu_border"), QStringLiteral("#2C3440"));
        tokens.insert(QStringLiteral("menu_text"), QStringLiteral("#D6DEE8"));
        tokens.insert(QStringLiteral("menu_selected_bg"), QStringLiteral("#2D7FD3"));
        tokens.insert(QStringLiteral("menu_selected_text"), QStringLiteral("#FFFFFF"));
        tokens.insert(QStringLiteral("tooltip_bg"), QStringLiteral("#1E232B"));
        tokens.insert(QStringLiteral("tooltip_border"), QStringLiteral("#2C3440"));
    } else {
        tokens.insert(QStringLiteral("text_primary"), QStringLiteral("#2C3E50"));
        tokens.insert(QStringLiteral("text_secondary"), QStringLiteral("#556270"));
        tokens.insert(QStringLiteral("accent_blue"), QStringLiteral("#4A90E2"));
        tokens.insert(QStringLiteral("accent_orange"), QStringLiteral("#fc9867"));
        tokens.insert(QStringLiteral("tab_text"), QStringLiteral("#2C3E50"));
        tokens.insert(QStringLiteral("tab_checked_bg"), QStringLiteral("rgba(255, 255, 255, 0.95)"));
        tokens.insert(QStringLiteral("type_checked_bg"), QStringLiteral("rgba(255, 255, 255, 0.95)"));
        tokens.insert(QStringLiteral("top_hover_bg"), QStringLiteral("#CCCCCC"));
        tokens.insert(QStringLiteral("line_edit_border"), QStringLiteral("#FFA07A"));
        tokens.insert(QStringLiteral("line_edit_border_focus"), QStringLiteral("#FF8C69"));
        tokens.insert(QStringLiteral("line_edit_bg"), QStringLiteral("rgba(255, 255, 255, 0.95)"));
        tokens.insert(QStringLiteral("badge_bg"), QStringLiteral("rgba(255, 255, 255, 0.22)"));
        tokens.insert(QStringLiteral("badge_text"), QStringLiteral("rgba(32, 48, 64, 0.92)"));
        tokens.insert(QStringLiteral("badge_border"), QStringLiteral("rgba(74, 144, 226, 0.14)"));
        tokens.insert(QStringLiteral("badge_bg_hover"), QStringLiteral("rgba(255, 255, 255, 0.30)"));
        tokens.insert(QStringLiteral("badge_border_hover"), QStringLiteral("rgba(74, 144, 226, 0.22)"));
        tokens.insert(QStringLiteral("menu_bg"), QStringLiteral("#FFFFFF"));
        tokens.insert(QStringLiteral("menu_border"), QStringLiteral("#E5E5E5"));
        tokens.insert(QStringLiteral("menu_text"), QStringLiteral("#1A1A1A"));
        tokens.insert(QStringLiteral("menu_selected_bg"), QStringLiteral("#E5E5E5"));
        tokens.insert(QStringLiteral("menu_selected_text"), QStringLiteral("#1A1A1A"));
        tokens.insert(QStringLiteral("tooltip_bg"), QStringLiteral("#FFFFFF"));
        tokens.insert(QStringLiteral("tooltip_border"), QStringLiteral("#E0E0E0"));
    }

    return tokens;
}
