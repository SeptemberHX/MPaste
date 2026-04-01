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

void WindowBlurHelper::enableBlurBehind(QWidget *widget, bool dark, int cornerRadius) {
#ifdef Q_OS_WIN
    if (!widget) return;
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());

    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    // Disable DWM's own corner rounding — we clip the window to our
    // own rounded shape via SetWindowRgn so the blur layer and the
    // painted border share the same corner radius.
    DWORD preference = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));

    if (cornerRadius > 0) {
        const int w = widget->width();
        const int h = widget->height();
        const int d = cornerRadius * 2;
        HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, d, d);
        SetWindowRgn(hwnd, rgn, TRUE);
    }

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
    Q_UNUSED(cornerRadius);
#endif
}
