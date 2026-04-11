#ifndef MPASTE_WINDOWBLURHELPER_H
#define MPASTE_WINDOWBLURHELPER_H

#include <QWidget>

namespace WindowBlurHelper {
    /// Enable acrylic blur behind a widget.
    /// Set extendFrame=false for popup menus to avoid DWM shadow.
    void enableBlurBehind(QWidget *widget, bool dark, bool extendFrame = true);

    /// Returns true on platforms where blur is not available (Ubuntu).
    /// Callers should use opaque backgrounds instead.
    inline bool useOpaqueFallback() {
#ifdef Q_OS_WIN
        return false;
#else
        return true;
#endif
    }
}

#endif // MPASTE_WINDOWBLURHELPER_H
