#ifndef MPASTE_WINDOWBLURHELPER_H
#define MPASTE_WINDOWBLURHELPER_H

#include <QWidget>

namespace WindowBlurHelper {
    /// Enable acrylic blur behind a widget.
    /// Set extendFrame=false for popup menus to avoid DWM shadow.
    void enableBlurBehind(QWidget *widget, bool dark, bool extendFrame = true);
}

#endif // MPASTE_WINDOWBLURHELPER_H
