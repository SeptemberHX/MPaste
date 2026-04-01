#include "WindowBlurHelper.h"

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>

namespace {

enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
};

enum WINDOWCOMPOSITIONATTRIB {
    WCA_ACCENT_POLICY = 19
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

DWORD accentColorFromArgb(const QColor &color) {
    return (static_cast<DWORD>(color.alpha()) << 24)
        | (static_cast<DWORD>(color.blue()) << 16)
        | (static_cast<DWORD>(color.green()) << 8)
        | static_cast<DWORD>(color.red());
}

} // anonymous namespace
#endif

void WindowBlurHelper::enableBlurBehind(QWidget *widget, bool dark) {
#ifdef Q_OS_WIN
    if (!widget) return;
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());

    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    DWORD preference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));

    auto user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto setWCA = reinterpret_cast<pfnSetWindowCompositionAttribute>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!setWCA) return;

    const QColor tint = dark ? QColor(30, 40, 55, 18) : QColor(231, 241, 244, 20);
    DWORD tintVal = accentColorFromArgb(tint);

    ACCENT_POLICY accent{};
    accent.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    accent.AccentFlags = 2;
    accent.GradientColor = tintVal;

    WINDOWCOMPOSITIONATTRIBDATA data{};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);
    setWCA(hwnd, &data);
#else
    Q_UNUSED(widget);
    Q_UNUSED(dark);
#endif
}
