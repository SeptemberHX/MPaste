// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 PlatformRelated 的实现逻辑。
// pos: utils 层中的 PlatformRelated 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
//
// Created by ragdoll on 2021/5/26.
//

#include "PlatformRelated.h"
#include <iostream>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHash>
#include <QUrl>

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

void XUtils::triggerPasteShortcut(Window winId, MPasteSettings::PasteShortcutMode mode) {
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

    const bool useTerminalPaste = (mode == MPasteSettings::CtrlShiftVShortcut)
        || (mode == MPasteSettings::AutoPasteShortcut && isTerminal);
    const bool useCtrlV = (mode == MPasteSettings::CtrlVShortcut)
        || (mode == MPasteSettings::AutoPasteShortcut && !isTerminal);

    xdo_send_keysequence_window_up(m_xdo, winId, "Alt", 0);
    if (useTerminalPaste) {
        xdo_send_keysequence_window(m_xdo, winId, "Control+Shift+v", 100);
    } else if (useCtrlV) {
        xdo_send_keysequence_window(m_xdo, winId, "Control+v", 100);
    } else if (mode == MPasteSettings::ShiftInsertShortcut) {
        xdo_send_keysequence_window(m_xdo, winId, "Shift+Insert", 100);
    } else {
        xdo_send_keysequence_window(m_xdo, winId, "Alt+Insert", 100);
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

void PlatformRelated::triggerPasteShortcut(MPasteSettings::PasteShortcutMode mode) {
    XUtils::triggerPasteShortcut(XUtils::currentWinId(), mode);
}

void PlatformRelated::startWindowTracking() {
    // Linux: not implemented yet
}

WId PlatformRelated::previousActiveWindow() {
    return PlatformRelated::currActiveWindow();
}

bool PlatformRelated::revealInFileManager(const QList<QUrl> &urls) {
    bool handled = false;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }

        const QString localPath = url.toLocalFile();
        const QFileInfo info(localPath);
        const QString targetPath = info.exists() ? info.absoluteFilePath() : localPath;
        const QString folderPath = QFileInfo(targetPath).absolutePath();
        if (folderPath.isEmpty()) {
            continue;
        }

        handled = QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath)) || handled;
    }
    return handled;
}

#elif defined(_WIN32)

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <QPixmap>
#include <QWindow>
#include <QGuiApplication>
#include <QScreen>

static HWND s_previousWindow = nullptr;
static HWINEVENTHOOK s_winEventHook = nullptr;

namespace {
QString canonicalLocalPath(const QUrl &url) {
    if (!url.isLocalFile()) {
        return {};
    }

    const QString localPath = url.toLocalFile();
    if (localPath.isEmpty()) {
        return {};
    }

    const QFileInfo info(localPath);
    return info.exists() ? info.absoluteFilePath() : QFileInfo(localPath).absoluteFilePath();
}

bool openFolderFallback(const QString &folderPath) {
    return !folderPath.isEmpty() && QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
}

bool revealGroupedPathsInExplorer(const QStringList &groupPaths) {
    if (groupPaths.isEmpty()) {
        return false;
    }

    const QString folderPath = QFileInfo(groupPaths.first()).absolutePath();
    if (folderPath.isEmpty()) {
        return false;
    }

    PIDLIST_ABSOLUTE folderPidl = nullptr;
    const HRESULT folderHr = SHParseDisplayName(reinterpret_cast<LPCWSTR>(folderPath.utf16()),
                                                nullptr, &folderPidl, 0, nullptr);
    if (FAILED(folderHr) || !folderPidl) {
        return openFolderFallback(folderPath);
    }

    QList<PIDLIST_ABSOLUTE> fullPidls;
    QVector<PCUITEMID_CHILD> childPidls;
    fullPidls.reserve(groupPaths.size());
    childPidls.reserve(groupPaths.size());

    for (const QString &path : groupPaths) {
        PIDLIST_ABSOLUTE itemPidl = nullptr;
        const HRESULT itemHr = SHParseDisplayName(reinterpret_cast<LPCWSTR>(path.utf16()),
                                                  nullptr, &itemPidl, 0, nullptr);
        if (FAILED(itemHr) || !itemPidl) {
            continue;
        }

        fullPidls.push_back(itemPidl);
        childPidls.push_back(ILFindLastID(itemPidl));
    }

    bool handled = false;
    if (!childPidls.isEmpty()) {
        handled = SUCCEEDED(SHOpenFolderAndSelectItems(folderPidl,
                                                       static_cast<UINT>(childPidls.size()),
                                                       childPidls.data(),
                                                       0));
    }

    if (!handled) {
        handled = openFolderFallback(folderPath);
    }

    for (PIDLIST_ABSOLUTE pidl : fullPidls) {
        CoTaskMemFree(pidl);
    }
    CoTaskMemFree(folderPidl);
    return handled;
}
}

void CALLBACK WinUtils::winEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                     LONG idObject, LONG idChild, DWORD eventThread, DWORD eventTime) {
    Q_UNUSED(hook); Q_UNUSED(event); Q_UNUSED(idObject); Q_UNUSED(idChild);
    Q_UNUSED(eventThread); Q_UNUSED(eventTime);

    if (!hwnd || !IsWindow(hwnd)) return;

    // 蹇界暐涓嶅彲瑙佺獥鍙?
    if (!IsWindowVisible(hwnd)) return;

    // 鑾峰彇绐楀彛鏍囬锛屾帓闄?MPaste 鑷韩
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

bool WinUtils::revealInExplorer(const QList<QUrl> &urls) {
    QStringList orderedFolders;
    QHash<QString, QStringList> groupedPaths;

    for (const QUrl &url : urls) {
        const QString path = canonicalLocalPath(url);
        if (path.isEmpty()) {
            continue;
        }

        const QString folderPath = QFileInfo(path).absolutePath();
        if (folderPath.isEmpty()) {
            continue;
        }

        QStringList &paths = groupedPaths[folderPath];
        if (paths.isEmpty()) {
            orderedFolders.push_back(folderPath);
        }
        if (!paths.contains(path, Qt::CaseInsensitive)) {
            paths.push_back(path);
        }
    }

    if (groupedPaths.isEmpty()) {
        return false;
    }

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitialize = SUCCEEDED(initHr);

    bool handled = false;
    for (const QString &folderPath : orderedFolders) {
        handled = revealGroupedPathsInExplorer(groupedPaths.value(folderPath)) || handled;
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }
    return handled;
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

    // 鑾峰彇鎵弿鐮?
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

    // 统一发送构造好的按键事件
    SendInput(inputCount, inputs, sizeof(INPUT));
}

void WinUtils::triggerPasteShortcut(HWND hwnd, MPasteSettings::PasteShortcutMode mode) {
    char className[256];
    GetClassNameA(hwnd, className, 256);

    const bool isTerminal = strcmp(className, "ConsoleWindowClass") == 0
        || strstr(className, "WindowsTerminal") != nullptr;

    switch (mode) {
        case MPasteSettings::AutoPasteShortcut:
            if (isTerminal) {
                simulateKeyPress('V', true, true, false);
            } else {
                simulateKeyPress('V', true, false, false);
            }
            break;
        case MPasteSettings::CtrlVShortcut:
            simulateKeyPress('V', true, false, false);
            break;
        case MPasteSettings::ShiftInsertShortcut:
            simulateKeyPress(VK_INSERT, false, true, false);
            break;
        case MPasteSettings::CtrlShiftVShortcut:
            simulateKeyPress('V', true, true, false);
            break;
        case MPasteSettings::AltInsertShortcut:
        default:
            simulateKeyPress(VK_INSERT, false, false, true);
            break;
    }
}

HWND WinUtils::currentWinId() {
    return GetForegroundWindow();
}

QPixmap WinUtils::getWindowIconWin32(HWND hwnd) {
    if (!hwnd) {
        return QPixmap(":/resources/resources/unknown.svg");
    }

    // 优先获取窗口的大图标
    HICON hIcon = nullptr;
    bool ownedIcon = false;  // 标记图标句柄是否需要在结束时释放
    SendMessageTimeout(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 1000, (PDWORD_PTR)&hIcon);
    if (hIcon) ownedIcon = true;

    // 如果没有大图标，再尝试获取小图标
    if (!hIcon) {
        SendMessageTimeout(hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 1000, (PDWORD_PTR)&hIcon);
        if (hIcon) ownedIcon = true;
    }

    if (!hIcon) {
        hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
    }

    if (!hIcon) {
        hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM);
    }

    if (!hIcon) {
        return QPixmap();
    }

    // 读取图标位图信息
    ICONINFO iconInfo;
    if (!GetIconInfo(hIcon, &iconInfo)) {
        return QPixmap();
    }

    // 获取图标位图尺寸
    BITMAP bm;
    GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bm);

    // 创建屏幕 DC 和兼容内存 DC
    HDC screenDC = GetDC(0);
    HDC memDC = CreateCompatibleDC(screenDC);

    // 创建兼容位图并选入内存 DC
    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, bm.bmWidth, bm.bmHeight);
    HGDIOBJ oldBitmap = SelectObject(memDC, hBitmap);

    // 将图标绘制到位图上
    DrawIcon(memDC, 0, 0, hIcon);

    // 从位图像素构造 QImage，再转换为 QPixmap
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bm.bmWidth;
    bmi.bmiHeader.biHeight = -bm.bmHeight; // 负高度表示自顶向下的位图
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    QImage image(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32_Premultiplied);
    GetDIBits(memDC, hBitmap, 0, bm.bmHeight, image.bits(), &bmi, DIB_RGB_COLORS);

    // 清理 GDI 资源
    SelectObject(memDC, oldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(0, screenDC);
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);

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
    return (WId)WinUtils::currentWinId();  // 浣跨敤 WId 绫诲瀷杞崲
}

void PlatformRelated::triggerPasteShortcut(MPasteSettings::PasteShortcutMode mode) {
    HWND target = WinUtils::getPreviousWindow();
    if (!target) {
        target = WinUtils::currentWinId();
    }
    WinUtils::triggerPasteShortcut(target, mode);
}

void PlatformRelated::startWindowTracking() {
    WinUtils::startWindowTracking();
}

WId PlatformRelated::previousActiveWindow() {
    return (WId)WinUtils::getPreviousWindow();
}

bool PlatformRelated::revealInFileManager(const QList<QUrl> &urls) {
    return WinUtils::revealInExplorer(urls);
}

#endif
