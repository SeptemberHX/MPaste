// input: Depends on Qt, ClipboardItem, ThemeManager.
// output: Shared anonymous-namespace-level helper functions for ScrollItemsWidget implementation files.
// pos: Internal header -- not part of the public API, only included by BoardXxx.cpp and ScrollItemsWidgetMV.cpp.
#ifndef BOARDINTERNALHELPERS_H
#define BOARDINTERNALHELPERS_H

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMenu>
#include <QObject>
#include <QPalette>
#include <QStandardPaths>
#include <QString>
#include <QStyle>
#include <QStyleFactory>

#include "data/ClipboardItem.h"
#include "utils/ThemeManager.h"
#include "WindowBlurHelper.h"

namespace BoardHelpers {

inline QString hoverButtonStyle(bool dark, int borderRadius, int padding) {
    const QString hoverColor = dark ? QStringLiteral("rgba(255, 255, 255, 31)")
                                    : QStringLiteral("rgba(0, 0, 0, 20)");
    return QString(R"(
        QToolButton {
            background: transparent;
            border: none;
            border-radius: %1px;
            padding: %2px;
        }
        QToolButton:hover {
            background: %3;
        }
    )").arg(borderRadius).arg(padding).arg(hoverColor);
}

inline void applyMenuTheme(QMenu *menu) {
    if (!menu) {
        return;
    }
    const bool dark = ThemeManager::instance()->isDark();

    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);

    const QString bg = QStringLiteral("transparent");
    const QString border = dark ? QStringLiteral("rgba(255, 255, 255, 30)") : QStringLiteral("rgba(0, 0, 0, 22)");
    const QString text = dark ? QStringLiteral("#D6DEE8") : QStringLiteral("#1E2936");
    const QString highlight = dark ? QStringLiteral("rgba(255, 255, 255, 22)") : QStringLiteral("rgba(0, 0, 0, 12)");
    const QString highlightText = dark ? QStringLiteral("#FFFFFF") : QStringLiteral("#1E2936");
    const QString separator = dark ? QStringLiteral("rgba(255, 255, 255, 20)") : QStringLiteral("rgba(0, 0, 0, 15)");

    menu->setStyleSheet(QStringLiteral(
        "QMenu {"
        " background-color: %1;"
        " border: 1px solid %2;"
        " border-radius: 8px;"
        " padding: 6px 0px;"
        " color: %3;"
        " font-size: 13px;"
        "}"
        "QMenu::item {"
        " padding: 6px 28px 6px 16px;"
        " border-radius: 6px;"
        " margin: 1px 6px;"
        " color: %3;"
        "}"
        "QMenu::item:selected {"
        " background-color: %4;"
        " color: %5;"
        "}"
        "QMenu::separator {"
        " height: 1px;"
        " background: %6;"
        " margin: 4px 12px;"
        "}"
        "QMenu::icon {"
        " padding-left: 8px;"
        "}").arg(bg, border, text, highlight, highlightText, separator));

    // Apply blur after the menu is shown (connect once).
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, dark]() {
        WindowBlurHelper::enableBlurBehind(menu, dark);
    });
}

inline bool looksBrokenTranslation(const QString &text) {
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

inline bool prefersChineseUi() {
    const QLocale locale = QLocale::system();
    return locale.language() == QLocale::Chinese
        || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);
}

inline QString plainTextPasteLabel() {
    QString label = QObject::tr("Paste as Plain Text");
    const QLocale locale = QLocale::system();
    if (label == QLatin1String("Paste as Plain Text") || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return QString::fromUtf16(u"\u7EAF\u6587\u672C\u7C98\u8D34");
        }
        return QStringLiteral("Paste as Plain Text");
    }
    return label;
}

inline QString detailsLabel() {
    QString label = QObject::tr("Details");
    const QLocale locale = QLocale::system();
    if (label == QLatin1String("Details") || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return QString::fromUtf16(u"\u8BE6\u60C5");
        }
        return QStringLiteral("Details");
    }
    return label;
}

inline QString openContainingFolderLabel() {
    QString label = QObject::tr("Open Containing Folder");
    const QLocale locale = QLocale::system();
    if (label == QLatin1String("Open Containing Folder") || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return QString::fromUtf16(u"\u6253\u5F00\u6240\u5728\u6587\u4EF6\u5939");
        }
        return QStringLiteral("Open Containing Folder");
    }
    return label;
}

inline QString aliasLabel() {
    QString label = QObject::tr("Alias");
    const QLocale locale = QLocale::system();
    if (label == QLatin1String("Alias") || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return QString::fromUtf16(u"\u522B\u540D");
        }
        return QStringLiteral("Alias");
    }
    return label;
}

inline QString favoriteActionLabel(bool favorite) {
    const QString key = favorite ? QStringLiteral("Remove from favorites") : QStringLiteral("Add to favorites");
    QString label = QObject::tr(key.toUtf8().constData());
    const QLocale locale = QLocale::system();
    if (label == key || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return favorite ? QString::fromUtf16(u"\u79FB\u9664\u6536\u85CF") : QString::fromUtf16(u"\u52A0\u5165\u6536\u85CF");
        }
        return key;
    }
    return label;
}

inline QString pinActionLabel(bool pinned) {
    const QString key = pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin to top");
    QString label = QObject::tr(key.toUtf8().constData());
    const QLocale locale = QLocale::system();
    if (label == key || looksBrokenTranslation(label)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return pinned ? QString::fromUtf16(u"\u53D6\u6D88\u7F6E\u9876") : QString::fromUtf16(u"\u7F6E\u9876");
        }
        return key;
    }
    return label;
}

inline QString saveItemLabel() {
    QString label = QObject::tr("Save");
    if (label == QLatin1String("Save") || looksBrokenTranslation(label)) {
        return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58") : QStringLiteral("Save");
    }
    return label;
}

inline QString deleteLabel() {
    QString label = QObject::tr("Delete");
    if (label == QLatin1String("Delete") || looksBrokenTranslation(label)) {
        return prefersChineseUi() ? QString::fromUtf16(u"\u5220\u9664") : QStringLiteral("Delete");
    }
    return label;
}

inline QString deleteSelectedLabel() {
    QString label = QObject::tr("Delete Selected");
    if (label == QLatin1String("Delete Selected") || looksBrokenTranslation(label)) {
        return prefersChineseUi() ? QString::fromUtf16(u"\u5220\u9664\u6240\u9009") : QStringLiteral("Delete Selected");
    }
    return label;
}

inline QString saveDialogTitle(ContentType type) {
    switch (type) {
        case Image:
            return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58\u56FE\u7247") : QStringLiteral("Save Image");
        case RichText:
            return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58 HTML") : QStringLiteral("Save HTML");
        case Text:
            return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58\u6587\u672C") : QStringLiteral("Save Text");
        default:
            return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58") : QStringLiteral("Save");
    }
}

inline QString saveFailedTitle() {
    return prefersChineseUi() ? QString::fromUtf16(u"\u4FDD\u5B58\u5931\u8D25") : QStringLiteral("Save Failed");
}

inline QString saveFailedMessage(const QString &reason) {
    if (prefersChineseUi()) {
        return reason.isEmpty()
            ? QString::fromUtf16(u"\u4FDD\u5B58\u6587\u4EF6\u5931\u8D25\u3002")
            : QString::fromUtf16(u"\u4FDD\u5B58\u6587\u4EF6\u5931\u8D25\u3002\n%1").arg(reason);
    }
    return reason.isEmpty()
        ? QStringLiteral("Failed to save the file.")
        : QStringLiteral("Failed to save the file.\n%1").arg(reason);
}

inline bool supportsSaveToFile(ContentType type) {
    return type == Text
        || type == RichText
        || type == Image;
}

inline QString sanitizeExportBaseName(QString baseName) {
    static const QString invalidChars = QStringLiteral("<>:\"/\\|?*");
    for (QChar &ch : baseName) {
        if (ch.unicode() < 32 || invalidChars.contains(ch)) {
            ch = QLatin1Char(' ');
        }
    }

    baseName = baseName.simplified();
    while (!baseName.isEmpty()
           && (baseName.endsWith(QLatin1Char(' ')) || baseName.endsWith(QLatin1Char('.')))) {
        baseName.chop(1);
    }
    return baseName;
}

inline QString exportBaseNameForItem(const ClipboardItem &item) {
    QString baseName = item.getAlias().trimmed();
    if (baseName.isEmpty()) {
        baseName = item.getTitle().trimmed();
    }
    if (baseName.isEmpty()) {
        baseName = item.getNormalizedText().section(QLatin1Char('\n'), 0, 0).trimmed();
    }
    if (baseName.isEmpty()) {
        baseName = item.getName().trimmed();
    }

    baseName = sanitizeExportBaseName(baseName);
    if (baseName.size() > 64) {
        baseName = baseName.left(64).trimmed();
    }
    if (!baseName.isEmpty()) {
        return baseName;
    }

    const QDateTime timestamp = item.getTime().isValid() ? item.getTime() : QDateTime::currentDateTime();
    return QStringLiteral("clipboard-%1").arg(timestamp.toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

inline QString defaultExportDirectory() {
    const QString documentsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documentsDir.isEmpty() ? QDir::homePath() : documentsDir;
}

inline QString suggestedExportPath(const ClipboardItem &item, const QString &suffix) {
    return QDir(defaultExportDirectory()).filePath(exportBaseNameForItem(item) + suffix);
}

inline QString ensureFileSuffix(const QString &filePath, const QString &suffix) {
    if (filePath.isEmpty() || !QFileInfo(filePath).suffix().isEmpty()) {
        return filePath;
    }
    return filePath + suffix;
}

inline QString imageSaveFilters() {
    return QStringLiteral("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;BMP Image (*.bmp);;WebP Image (*.webp);;All Files (*)");
}

inline QString defaultImageSuffixForFilter(const QString &selectedFilter) {
    const QString lowerFilter = selectedFilter.toLower();
    if (lowerFilter.contains(QStringLiteral("*.jpg")) || lowerFilter.contains(QStringLiteral("*.jpeg"))) {
        return QStringLiteral(".jpg");
    }
    if (lowerFilter.contains(QStringLiteral("*.bmp"))) {
        return QStringLiteral(".bmp");
    }
    if (lowerFilter.contains(QStringLiteral("*.webp"))) {
        return QStringLiteral(".webp");
    }
    return QStringLiteral(".png");
}

} // namespace BoardHelpers

#endif // BOARDINTERNALHELPERS_H
