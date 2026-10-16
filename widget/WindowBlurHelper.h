// output: Shared native blur capability and lifecycle helpers for windows, menus and dialogs.
#ifndef MPASTE_WINDOWBLURHELPER_H
#define MPASTE_WINDOWBLURHELPER_H

#include <QWidget>

namespace WindowBlurHelper {
    /// Enable acrylic blur behind a widget.
    /// Set extendFrame=false for popup menus to avoid DWM shadow.
    void enableBlurBehind(QWidget *widget, bool dark, bool extendFrame = true);

    /// Attach native compositor blur to a window or popup; false means unsupported.
    bool enableNativeBlur(QWidget *widget);

    bool hasNativeBlur();
    /// Solid fallback only when neither Windows acrylic nor native Wayland blur is available.
    bool useOpaqueFallback();

}

#endif // MPASTE_WINDOWBLURHELPER_H
