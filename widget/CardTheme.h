// input: Depends on ThemeManager for dark/light mode detection.
// output: Exposes a lightweight struct collecting all card rendering theme colors.
// pos: Widget-layer theme data used by ClipboardCardDelegate and card body renderers.
// update: If I change, update this header block.
#ifndef MPASTE_CARDTHEME_H
#define MPASTE_CARDTHEME_H

#include <QColor>
#include "utils/ThemeManager.h"

struct CardTheme {
    // Surface colors
    QColor baseSurface;
    QColor bodyTextColor;
    QColor footerTextColor;
    QColor subtleBorderColor;

    // Header-blend factor: how much base surface dominates over the icon-derived header color
    qreal headerBlendFactor;

    // Link card text colors (currently defined but reserved for future use)
    QColor linkTitleColor;
    QColor linkUrlColor;

    // Shortcut overlay
    QColor shortcutBgColor;
    QColor shortcutTextColor;

    // Loading / placeholder colors
    QColor placeholderBase;
    QColor placeholderHighlight;
    QColor placeholderTextColor;

    // Unavailable placeholder
    QColor unavailableBgColor;
    QColor unavailableTextColor;

    // File chip background
    QColor fileChipBgColor;

    // Favorite border glow
    QColor favoriteBorderColor;

    static CardTheme forCurrentTheme() {
        const bool dark = ThemeManager::instance()->isDark();
        CardTheme t;
        if (dark) {
            t.baseSurface           = QColor(QStringLiteral("#1C232C"));
            t.bodyTextColor         = QColor(QStringLiteral("#D8E1EB"));
            t.footerTextColor       = QColor(QStringLiteral("#93A2B3"));
            t.subtleBorderColor     = QColor(255, 255, 255, 24);
            t.headerBlendFactor     = 0.86;
            t.linkTitleColor        = QColor(QStringLiteral("#E6EDF5"));
            t.linkUrlColor          = QColor(QStringLiteral("#B7C3D4"));
            t.shortcutBgColor       = QColor(28, 35, 44, 210);
            t.shortcutTextColor     = QColor(200, 210, 225, 220);
            t.placeholderBase       = QColor(255, 255, 255, 18);
            t.placeholderHighlight  = QColor(255, 255, 255, 42);
            t.placeholderTextColor  = QColor(220, 230, 240, 120);
            t.unavailableBgColor    = QColor(255, 255, 255, 14);
            t.unavailableTextColor  = QColor(220, 230, 240, 110);
            t.fileChipBgColor       = QColor(255, 255, 255, 18);
            t.favoriteBorderColor   = QColor(247, 201, 93, 68);
        } else {
            t.baseSurface           = QColor(QStringLiteral("#FFFFFF"));
            t.bodyTextColor         = QColor(QStringLiteral("#30343B"));
            t.footerTextColor       = QColor(QStringLiteral("#556270"));
            t.subtleBorderColor     = QColor(0, 0, 0, 18);
            t.headerBlendFactor     = 0.93;
            t.linkTitleColor        = QColor(QStringLiteral("#555555"));
            t.linkUrlColor          = QColor(QStringLiteral("#555555"));
            t.shortcutBgColor       = QColor(245, 247, 250, 220);
            t.shortcutTextColor     = QColor(80, 95, 110, 220);
            t.placeholderBase       = QColor(0, 0, 0, 18);
            t.placeholderHighlight  = QColor(0, 0, 0, 36);
            t.placeholderTextColor  = QColor(90, 100, 115, 120);
            t.unavailableBgColor    = QColor(0, 0, 0, 12);
            t.unavailableTextColor  = QColor(90, 100, 115, 110);
            t.fileChipBgColor       = QColor(0, 0, 0, 10);
            t.favoriteBorderColor   = QColor(247, 201, 93, 68);
        }
        return t;
    }
};

#endif // MPASTE_CARDTHEME_H
