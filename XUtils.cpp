//
// Created by septemberhx on 2020/5/26.
//

#include "XUtils.h"
#include "xdo.h"

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
