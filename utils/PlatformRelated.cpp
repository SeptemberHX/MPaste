//
// Created by ragdoll on 2021/5/26.
//

#include "PlatformRelated.h"
#include <iostream>

#if defined(__linux__)
#include <QImage>
#include <QPainter>
#include "MPasteSettings.h"

xdo_t *XUtils::m_xdo = nullptr;
Display *XUtils::m_display = nullptr;

void XUtils::openXdo() {
    if (m_xdo == nullptr) {
        m_xdo = xdo_new(nullptr);
    }
}

void XUtils::openDisplay() {
    if (m_display == nullptr) {
        m_display = XOpenDisplay(nullptr);
    }
}

void XUtils::activeWindowX11(Window winId) {
    openXdo();
    xdo_activate_window(m_xdo, winId);
}

void XUtils::triggerPasteShortcut(Window winId) {
    openXdo();

    // Get window class to identify terminals
    XClassHint hint;
    Status status = XGetClassHint(m_display, winId, &hint);
    bool isTerminal = false;

    if (status) {
        // Check common terminal class names
        if (strcmp(hint.res_class, "XTerm") == 0 ||
            strcmp(hint.res_class, "konsole") == 0 ||
            strcmp(hint.res_class, "gnome-terminal") == 0 ||
            strcmp(hint.res_class, "terminator") == 0) {
            isTerminal = true;
        }
        XFree(hint.res_name);
        XFree(hint.res_class);
    }

    if (isTerminal) {
        xdo_send_keysequence_window_up(m_xdo, winId, "Alt", 100);
        xdo_send_keysequence_window(m_xdo, winId, "Control+Shift+v", 100);
    } else {
        xdo_send_keysequence_window_up(m_xdo, 0, "Alt", 0);
        xdo_send_keysequence_window(m_xdo, 0, "Control+v", 12000);
    }
}

Window XUtils::currentWinId() {
    openXdo();
    Window window = 0;
    int ret = xdo_get_active_window(m_xdo, &window);

    if (ret != XDO_SUCCESS) {
        openDisplay();
        Window root, parent, *children;
        unsigned int nchildren;
        Window focused;
        int revert_to;

        XGetInputFocus(m_display, &focused, &revert_to);
        if (focused != None) {
            return focused;
        }

        // Fallback: try to find the topmost window
        root = DefaultRootWindow(m_display);
        XQueryTree(m_display, root, &root, &parent, &children, &nchildren);
        if (children) {
            window = children[nchildren - 1];
            XFree(children);
        }
    }

    return window;
}

Atom XUtils::getAtom(const char* atomName) {
    openDisplay();
    return XInternAtom(m_display, atomName, False);
}

QPixmap XUtils::getWindowIconX11(Window winId) {
    openDisplay();

    Atom net_wm_icon = getAtom("_NET_WM_ICON");
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = nullptr;

    int status = XGetWindowProperty(m_display, winId, net_wm_icon,
                                  0, 1048576, False, XA_CARDINAL,
                                  &actual_type, &actual_format,
                                  &nitems, &bytes_after, &data);

    if (status == Success && data) {
        long *long_data = (long*) data;
        int width = long_data[0];
        int height = long_data[1];

        // Skip width and height values
        long_data += 2;

        QPixmap icon = convertFromNative(long_data, width, height);
        XFree(data);
        return icon;
    }

    // Fallback to default icon
    return QPixmap(":/resources/resources/unknown.svg");
}

QPixmap XUtils::convertFromNative(const void* data, int width, int height) {
    QImage image(width, height, QImage::Format_ARGB32);
    const long* long_data = static_cast<const long*>(data);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            long pixel = long_data[y * width + x];
            image.setPixel(x, y, pixel);
        }
    }

    return QPixmap::fromImage(image);
}

void PlatformRelated::activateWindow(WId wId) {
    XUtils::activeWindowX11((Window)wId);
}

QPixmap PlatformRelated::getWindowIcon(WId wId) {
    return XUtils::getWindowIconX11((Window)wId);
}

WId PlatformRelated::currActiveWindow() {
    return (WId)XUtils::currentWinId();
}

void PlatformRelated::triggerPasteShortcut() {
    XUtils::triggerPasteShortcut(XUtils::currentWinId());
}

void PlatformRelated::startWindowTracking() {
    // Linux: not implemented yet
}

WId PlatformRelated::previousActiveWindow() {
    return PlatformRelated::currActiveWindow();
}

#elif defined(_WIN32)

#include <windows.h>
#include <QPixmap>
#include <QWindow>
#include <QGuiApplication>
#include <QScreen>

static HWND s_previousWindow = nullptr;
static HWINEVENTHOOK s_winEventHook = nullptr;

void CALLBACK WinUtils::winEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                     LONG idObject, LONG idChild, DWORD eventThread, DWORD eventTime) {
    Q_UNUSED(hook); Q_UNUSED(event); Q_UNUSED(idObject); Q_UNUSED(idChild);
    Q_UNUSED(eventThread); Q_UNUSED(eventTime);

    if (!hwnd || !IsWindow(hwnd)) return;

    // 忽略不可见窗口
    if (!IsWindowVisible(hwnd)) return;

    // 获取窗口标题，排除 MPaste 自身
    wchar_t title[256];
    GetWindowTextW(hwnd, title, 256);
    QString windowTitle = QString::fromWCharArray(title);
    if (windowTitle == "MPaste") return;

    s_previousWindow = hwnd;
}

void WinUtils::startWindowTracking() {
    if (s_winEventHook) return;

    s_winEventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, winEventProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );
}

HWND WinUtils::getPreviousWindow() {
    return s_previousWindow;
}

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

    // 获取扫描码
    WORD scanCode = MapVirtualKey(key, MAPVK_VK_TO_VSC);
    WORD ctrlScan = MapVirtualKey(VK_CONTROL, MAPVK_VK_TO_VSC);
    WORD shiftScan = MapVirtualKey(VK_SHIFT, MAPVK_VK_TO_VSC);
    WORD altScan = MapVirtualKey(VK_MENU, MAPVK_VK_TO_VSC);

    // Press modifier keys
    if (ctrl) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_CONTROL;
        inputs[inputCount].ki.wScan = ctrlScan;
        inputs[inputCount].ki.dwFlags = KEYEVENTF_SCANCODE;
        inputCount++;
    }
    if (shift) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_SHIFT;
        inputs[inputCount].ki.wScan = shiftScan;
        inputs[inputCount].ki.dwFlags = KEYEVENTF_SCANCODE;
        inputCount++;
    }
    if (alt) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_MENU;
        inputs[inputCount].ki.wScan = altScan;
        inputs[inputCount].ki.dwFlags = KEYEVENTF_SCANCODE;
        inputCount++;
    }

    // Press the key
    inputs[inputCount].type = INPUT_KEYBOARD;
    inputs[inputCount].ki.wVk = key;
    inputs[inputCount].ki.wScan = scanCode;
    inputs[inputCount].ki.dwFlags = KEYEVENTF_SCANCODE;
    inputCount++;

    // Release the key
    inputs[inputCount].type = INPUT_KEYBOARD;
    inputs[inputCount].ki.wVk = key;
    inputs[inputCount].ki.wScan = scanCode;
    inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE;
    inputCount++;

    // Release modifier keys in reverse order
    if (alt) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_MENU;
        inputs[inputCount].ki.wScan = altScan;
        inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE;
        inputCount++;
    }
    if (shift) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_SHIFT;
        inputs[inputCount].ki.wScan = shiftScan;
        inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE;
        inputCount++;
    }
    if (ctrl) {
        inputs[inputCount].type = INPUT_KEYBOARD;
        inputs[inputCount].ki.wVk = VK_CONTROL;
        inputs[inputCount].ki.wScan = ctrlScan;
        inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE;
        inputCount++;
    }

    // 添加小延时确保窗口有足够时间处理事件
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
        simulateKeyPress(VK_INSERT, false, true, false); // Shift+Insert for normal windows
    }
}

HWND WinUtils::currentWinId() {
    return GetForegroundWindow();
}

QPixmap WinUtils::getWindowIconWin32(HWND hwnd) {
    if (!hwnd) {
        return QPixmap(":/resources/resources/unknown.svg");
    }

    // 获取窗口的大图标
    HICON hIcon = nullptr;
    bool ownedIcon = false;  // 标记是否需要自行销毁图标
    SendMessageTimeout(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 1000, (PDWORD_PTR)&hIcon);
    if (hIcon) ownedIcon = true;

    // 如果获取大图标失败，尝试获取小图标
    if (!hIcon) {
        SendMessageTimeout(hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 1000, (PDWORD_PTR)&hIcon);
        if (hIcon) ownedIcon = true;
    }

    // 如果通过消息获取失败，直接从窗口类获取（这些图标不能销毁）
    if (!hIcon) {
        hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
    }

    if (!hIcon) {
        hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM);
    }

    // 如果所有方法都失败了，返回空图标
    if (!hIcon) {
        return QPixmap();
    }

    // 获取图标信息
    ICONINFO iconInfo;
    if (!GetIconInfo(hIcon, &iconInfo)) {
        return QPixmap();
    }

    // 获取图标尺寸
    BITMAP bm;
    GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bm);

    // 创建设备上下文
    HDC screenDC = GetDC(0);
    HDC memDC = CreateCompatibleDC(screenDC);

    // 创建兼容位图
    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, bm.bmWidth, bm.bmHeight);
    HGDIOBJ oldBitmap = SelectObject(memDC, hBitmap);

    // 绘制图标
    DrawIcon(memDC, 0, 0, hIcon);

    // 转换为 QImage，再转换为 QPixmap
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bm.bmWidth;
    bmi.bmiHeader.biHeight = -bm.bmHeight; // 自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    QImage image(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32_Premultiplied);
    GetDIBits(memDC, hBitmap, 0, bm.bmHeight, image.bits(), &bmi, DIB_RGB_COLORS);

    // 清理资源
    SelectObject(memDC, oldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(0, screenDC);
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);

    // 只销毁通过 WM_GETICON 获取的图标副本，GetClassLongPtr 获取的是类图标不能销毁
    if (ownedIcon) {
        DestroyIcon(hIcon);
    }

    return QPixmap::fromImage(image);
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
    HWND target = WinUtils::getPreviousWindow();
    if (!target) {
        target = WinUtils::currentWinId();
    }
    WinUtils::triggerPasteShortcut(target);
}

void PlatformRelated::startWindowTracking() {
    WinUtils::startWindowTracking();
}

WId PlatformRelated::previousActiveWindow() {
    return (WId)WinUtils::getPreviousWindow();
}

#endif
