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
    xdo_send_keysequence_window_up(m_xdo, winId, "Alt", 0);
    KWindowInfo info(winId, NET::WMVisibleName);
    if (info.valid() && MPasteSettings::getInst()->isTerminalTitle(info.visibleName())) {
        xdo_send_keysequence_window(m_xdo, winId, "Control+Shift+v", 0);
    } else {
        // cannot work with jetbrains if using winId ???
        xdo_send_keysequence_window(m_xdo, XUtils::currentWinId(), "Control+v", 0);
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