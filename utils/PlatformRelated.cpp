//
// Created by ragdoll on 2021/5/26.
//

#include "PlatformRelated.h"

#if defined(__linux__)

#include <KWindowSystem>
#include <xdo.h>
#include <iostream>
#include "MPasteSettings.h"

xdo_t *XUtils::m_xdo = nullptr;
Display *XUtils::m_display = nullptr;

void XUtils::openXdo() {
    if (m_xdo == nullptr) {
        m_xdo = xdo_new(NULL);
    }
}

void XUtils::activeWindowX11(int winId) {
    openXdo();
    xdo_activate_window(m_xdo, winId);
}

void XUtils::triggerPasteShortcut(int winId) {
    openXdo();
    KWindowInfo info(winId, NET::WMVisibleName);
    if (info.valid() && MPasteSettings::getInst()->isTerminalTitle(info.visibleName())) {
        xdo_send_keysequence_window_up(m_xdo, winId, "Alt", 100);
        xdo_send_keysequence_window(m_xdo, winId, "Control+Shift+v", 100);
    } else {
        // let the xdotool handle the current window id
        //   it won't work if we try to use XUtils::currActiveWindow
        //   and the magic number 12000 also comes from the source code of xdotool
        //   without 12000, some applications like Edge will get twice paste items.
        xdo_send_keysequence_window_up(m_xdo, 0, "Alt", 0);
        xdo_send_keysequence_window(m_xdo, 0, "Control+v", 12000);
    }
}

int XUtils::currentWinId() {
    openXdo();
    Window window = 0;
    int ret = xdo_get_active_window(m_xdo, &window);

    if (ret == XDO_SUCCESS) {
        return window;
    } else {
        return KWindowSystem::activeWindow();
    }
}

void PlatformRelated::activateWindow(int wId) {
    XUtils::activeWindowX11(wId);
}

QPixmap PlatformRelated::getWindowIcon(int wId) {
    return KWindowSystem::self()->icon(wId);
}

int PlatformRelated::currActiveWindow() {
    return XUtils::currentWinId();
}

void PlatformRelated::triggerPasteShortcut() {
    XUtils::triggerPasteShortcut(MPasteSettings::getInst()->getCurrFocusWinId());
}

#elif defined(_WIN32)

#include <windows.h>
#include <QPixmap>
#include <QWindow>
#include <QGuiApplication>
#include <QScreen>

void WinUtils::activeWindowWin32(HWND hwnd) {
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
}

void WinUtils::simulateKeyPress(WORD key, bool ctrl, bool shift, bool alt) {
    INPUT inputs[8] = {};
    int inputCount = 0;

    // Press modifier keys
    if (ctrl) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_CONTROL;
        inputCount++;
    }
    if (shift) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_SHIFT;
        inputCount++;
    }
    if (alt) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_MENU;
        inputCount++;
    }

    // Press the key
    inputs[inputCount].type = INPUT_KEYBOARD;
    inputs[inputCount].ki.wVk = key;
    inputCount++;

    // Release the key
    inputs[inputCount].type = INPUT_KEYBOARD;
    inputs[inputCount].ki.wVk = key;
    inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
    inputCount++;

    // Release modifier keys
    if (alt) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_MENU;
        inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
        inputCount++;
    }
    if (shift) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_SHIFT;
        inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
        inputCount++;
    }
    if (ctrl) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_CONTROL;
        inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
        inputCount++;
    }

    SendInput(inputCount, inputs, sizeof(INPUT));
}

void WinUtils::triggerPasteShortcut(HWND hwnd) {
    char className[256];
    GetClassNameA(hwnd, className, 256);

    // Check if it's a terminal window (you might need to add more terminal class names)
    if (strcmp(className, "ConsoleWindowClass") == 0 ||  // CMD
        strstr(className, "WindowsTerminal") != nullptr) { // Windows Terminal
        simulateKeyPress('V', true, true, false); // Ctrl+Shift+V for terminals
    } else {
        simulateKeyPress('V', true, false, false); // Ctrl+V for normal windows
    }
}

HWND WinUtils::currentWinId() {
    return GetForegroundWindow();
}

QPixmap WinUtils::getWindowIconWin32(HWND hwnd) {
    HICON hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
    if (!hIcon) {
        hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
    }
    if (!hIcon) {
        hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
    }
    if (!hIcon) {
        hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

    if (hIcon) {
        ICONINFO iconInfo;
        if (GetIconInfo(hIcon, &iconInfo)) {
            HDC dc = GetDC(NULL);
            BITMAP bm;
            GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bm);

            int width = bm.bmWidth;
            int height = bm.bmHeight;

            QPixmap pixmap(width, height);
            pixmap.fill(Qt::transparent);

            HDC memDC = CreateCompatibleDC(dc);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, iconInfo.hbmColor);

            BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

            // Copy icon to pixmap
            BitBlt(memDC, 0, 0, width, height, dc, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBitmap);
            DeleteDC(memDC);
            ReleaseDC(NULL, dc);

            DeleteObject(iconInfo.hbmColor);
            DeleteObject(iconInfo.hbmMask);

            return pixmap;
        }
    }

    // Return a default icon if we couldn't get the window icon
    return QPixmap(":/resources/resources/unknown.svg");
}

// Implementation of PlatformRelated for Windows
void PlatformRelated::activateWindow(WId wId) {
    WinUtils::activeWindowWin32((HWND)wId);
}

QPixmap PlatformRelated::getWindowIcon(WId wId) {
    return WinUtils::getWindowIconWin32((HWND)wId);
}

WId PlatformRelated::currActiveWindow() {
    return (WId)WinUtils::currentWinId();  // 使用 WId 类型转换
}

void PlatformRelated::triggerPasteShortcut() {
    WinUtils::triggerPasteShortcut(WinUtils::currentWinId());
}

#endif