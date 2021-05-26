//
// Created by ragdoll on 2021/5/26.
//

#include "PlatformRelated.h"

#if defined(__linux__)

#include <KWindowSystem>
#include <xdo.h>

xdo_t *XUtils::m_xdo = nullptr;
Display *XUtils::m_display = nullptr;

void XUtils::openXdo() {
    if (m_xdo == nullptr) {
        m_xdo = xdo_new(NULL);
    }
}

void XUtils::activeWindowX11(int winId) {
    openXdo();
    xdo_activate_window(m_xdo, winId);
}


void PlatformRelated::activateWindow(int wId) {
    XUtils::activeWindowX11(wId);
}

QPixmap PlatformRelated::getWindowIcon(int wId) {
    return KWindowSystem::self()->icon(wId);
}

int PlatformRelated::currActiveWindow() {
    return KWindowSystem::activeWindow();
}

#endif