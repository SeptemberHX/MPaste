#include "HotKeyManager.h"
#include <QApplication>
#include <QAbstractNativeEventFilter>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#endif

class HotkeyEventFilter : public QAbstractNativeEventFilter {
public:
    explicit HotkeyEventFilter(HotkeyManager *manager) : manager(manager) {}

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override {
#ifdef Q_OS_WIN
        if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
            MSG *msg = static_cast<MSG *>(message);
            if (msg->message == WM_HOTKEY) {
                emit manager->hotkeyPressed();
                return true;
            }
        }
#elif defined(Q_OS_LINUX)
        if (eventType == "xcb_generic_event_t") {
            xcb_generic_event_t *event = static_cast<xcb_generic_event_t *>(message);
            if ((event->response_type & ~0x80) == XCB_KEY_PRESS) {
                xcb_key_press_event_t *kp = (xcb_key_press_event_t *)event;
                if (kp->detail == manager->d->keycode &&
                    (kp->state & manager->d->modifiers) == manager->d->modifiers) {
                    emit manager->hotkeyPressed();
                    return true;
                }
            }
        }
#endif
        return false;
    }

private:
    HotkeyManager *manager;
};

class HotkeyManager::Private {
public:
    class EventFilter : public QAbstractNativeEventFilter {
    public:
        explicit EventFilter(HotkeyManager *manager) : manager(manager) {}

        bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override {
#ifdef Q_OS_WIN
            if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
                MSG *msg = static_cast<MSG *>(message);
                if (msg->message == WM_HOTKEY) {
                    emit manager->hotkeyPressed();
                    return true;
                }
            }
#elif defined(Q_OS_LINUX)
            if (eventType == "xcb_generic_event_t") {
                xcb_generic_event_t *event = static_cast<xcb_generic_event_t *>(message);
                if ((event->response_type & ~0x80) == XCB_KEY_PRESS) {
                    xcb_key_press_event_t *kp = (xcb_key_press_event_t *)event;
                    if (kp->detail == manager->d->keycode &&
                        (kp->state & manager->d->modifiers) == manager->d->modifiers) {
                        emit manager->hotkeyPressed();
                        return true;
                        }
                }
            }
#endif
            return false;
        }

    private:
        HotkeyManager *manager;
    };

    EventFilter *eventFilter;
#ifdef Q_OS_WIN
    int hotkeyId;
#elif defined(Q_OS_LINUX)
    xcb_connection_t *connection;
    xcb_window_t root;
    xcb_key_symbols_t *keySymbols;
    uint32_t keycode;
    uint32_t modifiers;
#endif
};

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->eventFilter = new Private::EventFilter(this);
    qApp->installNativeEventFilter(d->eventFilter);

#ifdef Q_OS_WIN
    d->hotkeyId = 0;
#elif defined(Q_OS_LINUX)
    // 初始化 XCB 连接
    d->connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(d->connection)) {
        qWarning("Failed to connect to X server");
        return;
    }

    // 获取根窗口
    const xcb_setup_t *setup = xcb_get_setup(d->connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    d->root = iter.data->root;

    // 初始化按键符号表
    d->keySymbols = xcb_key_symbols_alloc(d->connection);
    d->keycode = 0;
    d->modifiers = 0;
#endif
}

HotkeyManager::~HotkeyManager()
{
    unregisterHotkey();
    qApp->removeNativeEventFilter(d->eventFilter);
    delete d->eventFilter;

#ifdef Q_OS_LINUX
    if (d->keySymbols) {
        xcb_key_symbols_free(d->keySymbols);
    }
    if (d->connection) {
        xcb_disconnect(d->connection);
    }
#endif

    delete d;
}

bool HotkeyManager::registerHotkey(const QKeySequence &keySequence)
{
    unregisterHotkey();

#ifdef Q_OS_WIN
    int modifiers = 0;
    int key = 0;

    if (keySequence.isEmpty())
        return false;

    Qt::KeyboardModifiers qtMods = Qt::KeyboardModifiers(keySequence[0] & Qt::KeyboardModifierMask);
    key = keySequence[0] & ~Qt::KeyboardModifierMask;

    if (qtMods & Qt::AltModifier)
        modifiers |= MOD_ALT;
    if (qtMods & Qt::ControlModifier)
        modifiers |= MOD_CONTROL;
    if (qtMods & Qt::ShiftModifier)
        modifiers |= MOD_SHIFT;

    d->hotkeyId = 1;
    return RegisterHotKey(nullptr, d->hotkeyId, modifiers, key);

#elif defined(Q_OS_LINUX)
    if (keySequence.isEmpty() || !d->connection || !d->keySymbols)
        return false;

    // 解析修饰键
    d->modifiers = 0;
    Qt::KeyboardModifiers qtMods = Qt::KeyboardModifiers(keySequence[0] & Qt::KeyboardModifierMask);

    if (qtMods & Qt::ShiftModifier)
        d->modifiers |= XCB_MOD_MASK_SHIFT;
    if (qtMods & Qt::ControlModifier)
        d->modifiers |= XCB_MOD_MASK_CONTROL;
    if (qtMods & Qt::AltModifier)
        d->modifiers |= XCB_MOD_MASK_1;

    // 获取键码
    int keycode = keySequence[0] & ~Qt::KeyboardModifierMask;
    xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(d->keySymbols, keycode);
    if (!keycodes) {
        return false;
    }

    d->keycode = keycodes[0];  // 使用第一个键码
    free(keycodes);

    // 注册全局热键
    xcb_grab_key(d->connection, 1, d->root,
                 d->modifiers, d->keycode,
                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

    xcb_flush(d->connection);
    return true;
#else
    return false;
#endif
}

void HotkeyManager::unregisterHotkey()
{
#ifdef Q_OS_WIN
    if (d->hotkeyId != 0) {
        UnregisterHotKey(nullptr, d->hotkeyId);
        d->hotkeyId = 0;
    }
#elif defined(Q_OS_LINUX)
    if (d->connection && d->keycode != 0) {
        xcb_ungrab_key(d->connection, d->keycode, d->root, d->modifiers);
        xcb_flush(d->connection);
        d->keycode = 0;
        d->modifiers = 0;
    }
#endif
}