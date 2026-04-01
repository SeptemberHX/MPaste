#ifndef MPASTE_WINDOWBLURHELPER_H
#define MPASTE_WINDOWBLURHELPER_H

#include <QWidget>

namespace WindowBlurHelper {
    void enableBlurBehind(QWidget *widget, bool dark, int cornerRadius = 0);
}

#endif // MPASTE_WINDOWBLURHELPER_H
