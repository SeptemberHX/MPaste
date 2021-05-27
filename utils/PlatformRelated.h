//
// Created by ragdoll on 2021/5/26.
//

#ifndef MPASTE_PLATFORMRELATED_H
#define MPASTE_PLATFORMRELATED_H

// must included, otherwise there is compile error on ubuntu 21.04
#include <QtCore>
#include <QPixmap>

class PlatformRelated {

public:
    static void activateWindow(int wId);
    static QPixmap getWindowIcon(int wId);
    static int currActiveWindow();
};

#if defined(__linux__)

extern "C" {
#include <xdo.h>
#include <X11/X.h>
}

class XUtils {

public:
    static void activeWindowX11(int winId);

    static xdo_t *m_xdo;
    static Display *m_display;
private:
    static void openXdo();
    static void openDisplay();
};

#elif defined(__WIN32__)

#endif

#endif //MPASTE_PLATFORMRELATED_H
