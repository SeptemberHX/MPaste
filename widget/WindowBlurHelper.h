#ifndef MPASTE_WINDOWBLURHELPER_H
#define MPASTE_WINDOWBLURHELPER_H

#include <QWidget>

namespace WindowBlurHelper {
    // opacity: 0 = fully transparent blur, 100 = more opaque tint (default 40)
    void enableBlurBehind(QWidget *widget, bool dark, int opacity = 40);
}

#endif // MPASTE_WINDOWBLURHELPER_H
