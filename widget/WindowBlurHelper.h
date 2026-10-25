// output: Shared native blur capability and lifecycle helpers for windows, menus and dialogs.
#ifndef MPASTE_WINDOWBLURHELPER_H
#define MPASTE_WINDOWBLURHELPER_H

#include <QWidget>

namespace WindowBlurHelper {
    /// Enable acrylic blur behind a widget.
    /// Set extendFrame=false for popup menus to avoid DWM shadow.
    void enableBlurBehind(QWidget *widget, bool dark, bool extendFrame = true);

    /// Backdrop for a window that paints its own rounded body of `radius`, `inset`
    /// from its edges. Linux blurs exactly that shape. Windows gets no acrylic:
    /// DWM can only round it to 8 px, which would show past larger corners, so the
    /// window's painted per-pixel alpha alone gives the shape (no DWM corner or border).
    void enableShapedBackdrop(QWidget *widget, bool dark, qreal radius, qreal inset = 0);

    /// Attach native compositor blur to a window or popup; false means unsupported.
    bool enableNativeBlur(QWidget *widget);

    bool hasNativeBlur();
    /// Solid fallback only when neither Windows acrylic nor native Wayland blur is available.
    bool useOpaqueFallback();

}

#endif // MPASTE_WINDOWBLURHELPER_H
