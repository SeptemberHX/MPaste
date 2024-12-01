//
// Created by ragdoll on 2021/5/26.
//

#ifndef MPASTE_PLATFORMRELATED_H
#define MPASTE_PLATFORMRELATED_H

// must included, otherwise there is compile error on ubuntu 21.04
#include <QtCore>
#include <QPixmap>

class PlatformRelated {

public:
    static void activateWindow(WId wId);  // 使用 WId 而不是 int
    static QPixmap getWindowIcon(WId wId);
    static WId currActiveWindow();  // 返回 WId
    static void triggerPasteShortcut();
};

#if defined(__linux__)

extern "C" {
#include <xdo.h>
#include <X11/X.h>
}

class XUtils {

public:
    static void activeWindowX11(int winId);
    static void triggerPasteShortcut(int winId);
    static int currentWinId();

    static xdo_t *m_xdo;
    static Display *m_display;
private:
    static void openXdo();
    static void openDisplay();
};

#elif defined(__WIN32__)
#include <windows.h>

class WinUtils {
public:
    static void activeWindowWin32(HWND hwnd);
    static void triggerPasteShortcut(HWND hwnd);
    static HWND currentWinId();
    static QPixmap getWindowIconWin32(HWND hwnd);

private:
    static void simulateKeyPress(WORD key, bool ctrl = false, bool shift = false, bool alt = false);
};

#endif

#endif //MPASTE_PLATFORMRELATED_H
