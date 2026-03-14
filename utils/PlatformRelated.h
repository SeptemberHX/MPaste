// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 PlatformRelated 的声明接口。
// pos: utils 层中的 PlatformRelated 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
//
// Created by ragdoll on 2021/5/26.
//

#ifndef MPASTE_PLATFORMRELATED_H
#define MPASTE_PLATFORMRELATED_H

// must included, otherwise there is compile error on ubuntu 21.04
#include <QtCore>
#include <QPixmap>
#include <QUrl>
#include "MPasteSettings.h"

class PlatformRelated {

public:
    static void activateWindow(WId wId);
    static QPixmap getWindowIcon(WId wId);
    static WId currActiveWindow();
    static void triggerPasteShortcut(MPasteSettings::PasteShortcutMode mode = MPasteSettings::AutoPasteShortcut);
    static void startWindowTracking();
    static WId previousActiveWindow();
    static bool revealInFileManager(const QList<QUrl> &urls);
};

#if defined(__linux__)

extern "C" {
#include <xdo.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
}

// X11 headers define macros like `KeyPress`/`KeyRelease` that conflict with Qt enums
// (e.g. `QEvent::KeyPress`). Undefine the known offenders to keep Qt code compiling.
#ifdef KeyPress
#undef KeyPress
#endif
#ifdef KeyRelease
#undef KeyRelease
#endif

class XUtils {
public:
    static void activeWindowX11(Window winId);
    static void triggerPasteShortcut(Window winId, MPasteSettings::PasteShortcutMode mode);
    static Window currentWinId();
    static QPixmap getWindowIconX11(Window winId);

    static xdo_t *m_xdo;
    static Display *m_display;

private:
    static void openXdo();
    static void openDisplay();
    static Atom getAtom(const char* atomName);
    static QPixmap convertFromNative(const void* data, int width, int height);
};

#elif defined(_WIN32)
#include <windows.h>

class WinUtils {
public:
    static void activeWindowWin32(HWND hwnd);
    static void triggerPasteShortcut(HWND hwnd, MPasteSettings::PasteShortcutMode mode);
    static HWND currentWinId();
    static QPixmap getWindowIconWin32(HWND hwnd);
    static void startWindowTracking();
    static HWND getPreviousWindow();
    static bool revealInExplorer(const QList<QUrl> &urls);

private:
    static void simulateKeyPress(WORD key, bool ctrl = false, bool shift = false, bool alt = false);
    static void CALLBACK winEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                      LONG idObject, LONG idChild, DWORD eventThread, DWORD eventTime);
};

#endif

#endif //MPASTE_PLATFORMRELATED_H
