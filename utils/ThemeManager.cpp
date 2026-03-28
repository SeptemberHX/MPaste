// input: Depends on ThemeManager.h, MPasteSettings, Qt application and style hints.
// output: Applies theme palette and merged QSS to the application.
// pos: utils layer theme service implementation.
// update: If I change, update this header block and utils/README.md.
#include "ThemeManager.h"

#include <QFile>
#include <QGuiApplication>
#include <QStyleHints>
#include <QApplication>

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
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this]() {
            if (MPasteSettings::getInst()->getThemeMode() == MPasteSettings::ThemeSystem) {
                applyTheme();
            }
        });
    }

    applyTheme();
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
    const QString content = QLatin1String(styleFile.readAll());
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
        tokens.insert(QStringLiteral("tab_checked_bg"), QStringLiteral("rgba(255, 255, 255, 28)"));
        tokens.insert(QStringLiteral("type_checked_bg"), QStringLiteral("rgba(255, 255, 255, 24)"));
        tokens.insert(QStringLiteral("tab_border_width"), QStringLiteral("1px"));
        tokens.insert(QStringLiteral("tab_clipboard_border_color"), QStringLiteral("rgba(255, 255, 255, 36)"));
        tokens.insert(QStringLiteral("tab_starred_border_color"), QStringLiteral("rgba(255, 255, 255, 36)"));
        tokens.insert(QStringLiteral("type_checked_border_color"), QStringLiteral("rgba(255, 255, 255, 32)"));
        tokens.insert(QStringLiteral("top_hover_bg"), QStringLiteral("rgba(28, 34, 44, 235)"));
        tokens.insert(QStringLiteral("line_edit_border"), QStringLiteral("#3A4655"));
        tokens.insert(QStringLiteral("line_edit_border_focus"), QStringLiteral("#4A90E2"));
        tokens.insert(QStringLiteral("line_edit_bg"), QStringLiteral("rgba(24, 29, 36, 242)"));
        tokens.insert(QStringLiteral("badge_bg"), QStringLiteral("rgba(20, 26, 34, 140)"));
        tokens.insert(QStringLiteral("badge_text"), QStringLiteral("#F2F6FB"));
        tokens.insert(QStringLiteral("badge_border"), QStringLiteral("rgba(116, 154, 214, 64)"));
        tokens.insert(QStringLiteral("badge_bg_hover"), QStringLiteral("rgba(30, 38, 48, 179)"));
        tokens.insert(QStringLiteral("badge_border_hover"), QStringLiteral("rgba(116, 154, 214, 89)"));
        tokens.insert(QStringLiteral("menu_bg"), QStringLiteral("#1E232B"));
        tokens.insert(QStringLiteral("menu_border"), QStringLiteral("#2C3440"));
        tokens.insert(QStringLiteral("menu_text"), QStringLiteral("#D6DEE8"));
        tokens.insert(QStringLiteral("menu_selected_bg"), QStringLiteral("#2D7FD3"));
        tokens.insert(QStringLiteral("menu_selected_text"), QStringLiteral("#FFFFFF"));
        tokens.insert(QStringLiteral("tooltip_bg"), QStringLiteral("#1E232B"));
        tokens.insert(QStringLiteral("tooltip_border"), QStringLiteral("#2C3440"));
        // Glass tab/filter tokens (dark)
        tokens.insert(QStringLiteral("glass_tab_bg"), QStringLiteral("rgba(255, 255, 255, 18)"));
        tokens.insert(QStringLiteral("glass_tab_border"), QStringLiteral("rgba(255, 255, 255, 30)"));
        tokens.insert(QStringLiteral("glass_tab_hover_bg"), QStringLiteral("rgba(255, 255, 255, 30)"));
        tokens.insert(QStringLiteral("glass_tab_hover_border"), QStringLiteral("rgba(255, 255, 255, 42)"));
        tokens.insert(QStringLiteral("glass_tab_checked_bg"), QStringLiteral("rgba(255, 255, 255, 36)"));
        tokens.insert(QStringLiteral("glass_tab_checked_border"), QStringLiteral("rgba(255, 255, 255, 50)"));
        tokens.insert(QStringLiteral("glass_filter_bg"), QStringLiteral("rgba(255, 255, 255, 14)"));
        tokens.insert(QStringLiteral("glass_filter_border"), QStringLiteral("rgba(255, 255, 255, 24)"));
        tokens.insert(QStringLiteral("glass_filter_hover_bg"), QStringLiteral("rgba(255, 255, 255, 26)"));
        tokens.insert(QStringLiteral("glass_filter_hover_border"), QStringLiteral("rgba(255, 255, 255, 36)"));
        tokens.insert(QStringLiteral("glass_filter_checked_bg"), QStringLiteral("rgba(255, 255, 255, 32)"));
        tokens.insert(QStringLiteral("glass_filter_checked_border"), QStringLiteral("rgba(255, 255, 255, 46)"));
    } else {
        tokens.insert(QStringLiteral("text_primary"), QStringLiteral("#2C3E50"));
        tokens.insert(QStringLiteral("text_secondary"), QStringLiteral("#556270"));
        tokens.insert(QStringLiteral("accent_blue"), QStringLiteral("#4A90E2"));
        tokens.insert(QStringLiteral("accent_orange"), QStringLiteral("#fc9867"));
        tokens.insert(QStringLiteral("tab_text"), QStringLiteral("#2C3E50"));
        tokens.insert(QStringLiteral("tab_checked_bg"), QStringLiteral("rgba(255, 255, 255, 242)"));
        tokens.insert(QStringLiteral("type_checked_bg"), QStringLiteral("rgba(255, 255, 255, 242)"));
        tokens.insert(QStringLiteral("tab_border_width"), QStringLiteral("2px"));
        tokens.insert(QStringLiteral("tab_clipboard_border_color"), QStringLiteral("#4A90E2"));
        tokens.insert(QStringLiteral("tab_starred_border_color"), QStringLiteral("#fc9867"));
        tokens.insert(QStringLiteral("type_checked_border_color"), QStringLiteral("#4A90E2"));
        tokens.insert(QStringLiteral("top_hover_bg"), QStringLiteral("#CCCCCC"));
        tokens.insert(QStringLiteral("line_edit_border"), QStringLiteral("#FFA07A"));
        tokens.insert(QStringLiteral("line_edit_border_focus"), QStringLiteral("#FF8C69"));
        tokens.insert(QStringLiteral("line_edit_bg"), QStringLiteral("rgba(255, 255, 255, 242)"));
        tokens.insert(QStringLiteral("badge_bg"), QStringLiteral("rgba(255, 255, 255, 56)"));
        tokens.insert(QStringLiteral("badge_text"), QStringLiteral("rgba(32, 48, 64, 235)"));
        tokens.insert(QStringLiteral("badge_border"), QStringLiteral("rgba(74, 144, 226, 36)"));
        tokens.insert(QStringLiteral("badge_bg_hover"), QStringLiteral("rgba(255, 255, 255, 77)"));
        tokens.insert(QStringLiteral("badge_border_hover"), QStringLiteral("rgba(74, 144, 226, 56)"));
        tokens.insert(QStringLiteral("menu_bg"), QStringLiteral("#FFFFFF"));
        tokens.insert(QStringLiteral("menu_border"), QStringLiteral("#E5E5E5"));
        tokens.insert(QStringLiteral("menu_text"), QStringLiteral("#1A1A1A"));
        tokens.insert(QStringLiteral("menu_selected_bg"), QStringLiteral("#E5E5E5"));
        tokens.insert(QStringLiteral("menu_selected_text"), QStringLiteral("#1A1A1A"));
        tokens.insert(QStringLiteral("tooltip_bg"), QStringLiteral("#FFFFFF"));
        tokens.insert(QStringLiteral("tooltip_border"), QStringLiteral("#E0E0E0"));
        // Glass tab/filter tokens (light) — keep original opaque style
        tokens.insert(QStringLiteral("glass_tab_bg"), QStringLiteral("transparent"));
        tokens.insert(QStringLiteral("glass_tab_border"), QStringLiteral("transparent"));
        tokens.insert(QStringLiteral("glass_tab_hover_bg"), QStringLiteral("rgba(255, 255, 255, 200)"));
        tokens.insert(QStringLiteral("glass_tab_hover_border"), QStringLiteral("transparent"));
        tokens.insert(QStringLiteral("glass_tab_checked_bg"), QStringLiteral("rgba(255, 255, 255, 242)"));
        tokens.insert(QStringLiteral("glass_tab_checked_border"), QStringLiteral("#4A90E2"));
        tokens.insert(QStringLiteral("glass_filter_bg"), QStringLiteral("transparent"));
        tokens.insert(QStringLiteral("glass_filter_border"), QStringLiteral("transparent"));
        tokens.insert(QStringLiteral("glass_filter_hover_bg"), QStringLiteral("rgba(255, 255, 255, 200)"));
        tokens.insert(QStringLiteral("glass_filter_hover_border"), QStringLiteral("transparent"));
        tokens.insert(QStringLiteral("glass_filter_checked_bg"), QStringLiteral("rgba(255, 255, 255, 242)"));
        tokens.insert(QStringLiteral("glass_filter_checked_border"), QStringLiteral("#4A90E2"));
    }

    return tokens;
}
