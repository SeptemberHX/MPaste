//
// Created by septemberhx on 2020/5/26.
//

#ifndef DDE_TOP_PANEL_XUTILS_H
#define DDE_TOP_PANEL_XUTILS_H

extern "C" {
    #include <xdo.h>
}

// something strange happens...

//struct context_t {
//    xdo_t *xdo;
//};


/**
 * This class is implemented before I realized that deepin depends on kwin now.
 *   So they can be replaced by the KWindowSystem::XXX
 *   However, those functions work well, so I do not plan to change them unless bugs happen
 */
class XUtils {

public:
    static void activeWindowX11(int winId);

    static xdo_t *m_xdo;
    static Display *m_display;
private:
    static void openXdo();
    static void openDisplay();
};


#endif //DDE_TOP_PANEL_XUTILS_H
