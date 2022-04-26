//
// Created by ragdoll on 2021/5/26.
//

#include "PlatformRelated.h"

#if defined(__linux__)

#include <KWindowSystem>
#include <xdo.h>
#include <iostream>
#include "MPasteSettings.h"

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

void XUtils::triggerPasteShortcut(int winId) {
    openXdo();
    KWindowInfo info(winId, NET::WMVisibleName);
    if (info.valid() && MPasteSettings::getInst()->isTerminalTitle(info.visibleName())) {
        xdo_send_keysequence_window_up(m_xdo, winId, "Alt", 100);
        xdo_send_keysequence_window(m_xdo, winId, "Control+Shift+v", 100);
    } else {
        // let the xdotool handle the current window id
        //   it won't work if we try to use XUtils::currActiveWindow
        //   and the magic number 12000 also comes from the source code of xdotool
        //   without 12000, some applications like Edge will get twice paste items.
        xdo_send_keysequence_window_up(m_xdo, 0, "Alt", 0);
        xdo_send_keysequence_window(m_xdo, 0, "Control+v", 12000);
    }
}

int XUtils::currentWinId() {
    openXdo();
    Window window = 0;
    int ret = xdo_get_active_window(m_xdo, &window);

    if (ret == XDO_SUCCESS) {
        return window;
    } else {
        return KWindowSystem::activeWindow();
    }
}

void PlatformRelated::activateWindow(int wId) {
    XUtils::activeWindowX11(wId);
}

QPixmap PlatformRelated::getWindowIcon(int wId) {
    return KWindowSystem::self()->icon(wId);
}

int PlatformRelated::currActiveWindow() {
    return XUtils::currentWinId();
}

void PlatformRelated::triggerPasteShortcut() {
    XUtils::triggerPasteShortcut(MPasteSettings::getInst()->getCurrFocusWinId());
}

#endif

#if defined(__APPLE__)

void PlatformRelated::activateWindow(int wId) {
}


QPixmap PlatformRelated::getWindowIcon(int wId) {
    return QPixmap();
}

int PlatformRelated::currActiveWindow() {
    return 0;
}

void PlatformRelated::triggerPasteShortcut() {

}

#endif
