// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: 提供 HotKeyManager 的实现逻辑。
// pos: utils 层中的 HotKeyManager 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
// note: Linux global hotkeys rely on X11 passive key grabs via Qt's XCB connection; Wayland sessions cannot be supported here.
// HotKeyManager.cpp
#include "HotKeyManager.h"
#include <QApplication>
#include <QAbstractNativeEventFilter>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#endif

class HotkeyManager::Private {
public:
    explicit Private(HotkeyManager *q) : q(q) {
        eventFilter = new EventFilter(this);
        qApp->installNativeEventFilter(eventFilter);

#ifdef Q_OS_WIN
        hotkeyId = 0;
#elif defined(Q_OS_LINUX)
        connection = nullptr;
        root = 0;
        keySymbols = nullptr;
        keycode = 0;
        modifiers = 0;

        // Initialize XCB connection
        if (auto *x11App = qApp->nativeInterface<QNativeInterface::QX11Application>()) {
            connection = x11App->connection();
        }
        if (!connection) {
            qWarning("Global hotkey is unavailable: not running on X11 (Wayland session?). "
                     "Consider configuring a system shortcut to launch MPaste instead.");
            return;
        }

        // Get root window
        const xcb_setup_t *setup = xcb_get_setup(connection);
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
        root = iter.data->root;

        // Initialize key symbols
        keySymbols = xcb_key_symbols_alloc(connection);
        keycode = 0;
        modifiers = 0;
#endif
    }

    ~Private() {
        unregisterHotkey();
#ifdef Q_OS_LINUX
        if (keySymbols) {
            xcb_key_symbols_free(keySymbols);
        }
#endif
        qApp->removeNativeEventFilter(eventFilter);
        delete eventFilter;
    }

    bool registerHotkey(const QKeySequence &keySequence) {
#ifdef Q_OS_WIN
        if (keySequence.isEmpty())
            return false;

        // Parse modifiers
        int modifiers = 0;
        Qt::KeyboardModifiers qtMods = Qt::KeyboardModifiers(keySequence[0] & Qt::KeyboardModifierMask);

        if (qtMods & Qt::AltModifier)
            modifiers |= MOD_ALT;
        if (qtMods & Qt::ControlModifier)
            modifiers |= MOD_CONTROL;
        if (qtMods & Qt::ShiftModifier)
            modifiers |= MOD_SHIFT;

        // Get the key
        int key = keySequence[0] & ~Qt::KeyboardModifierMask;

        // Register hotkey
        hotkeyId = 1;  // You might want to generate unique IDs
        return RegisterHotKey(nullptr, hotkeyId, modifiers, key);

#elif defined(Q_OS_LINUX)
        if (keySequence.isEmpty() || !connection || !keySymbols)
            return false;

        // Parse modifiers
        modifiers = 0;
        Qt::KeyboardModifiers qtMods = Qt::KeyboardModifiers(keySequence[0] & Qt::KeyboardModifierMask);

        if (qtMods & Qt::ShiftModifier)
            modifiers |= XCB_MOD_MASK_SHIFT;
        if (qtMods & Qt::ControlModifier)
            modifiers |= XCB_MOD_MASK_CONTROL;
        if (qtMods & Qt::AltModifier)
            modifiers |= XCB_MOD_MASK_1;
        if (qtMods & Qt::MetaModifier)
            modifiers |= XCB_MOD_MASK_4;

        // Get keycode
        int key = keySequence[0] & ~Qt::KeyboardModifierMask;
        xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(keySymbols, static_cast<xcb_keysym_t>(key));
        if (!keycodes) {
            return false;
        }

        keycode = keycodes[0];
        free(keycodes);

        // Register global hotkey
        // X11 passive grabs match modifiers exactly; include common lock masks (CapsLock/NumLock)
        // so the hotkey works regardless of lock state.
        const uint16_t baseModifiers = static_cast<uint16_t>(modifiers);
        const uint16_t lockMasks[] = {
            0,
            static_cast<uint16_t>(XCB_MOD_MASK_LOCK),              // CapsLock
            static_cast<uint16_t>(XCB_MOD_MASK_2),                 // NumLock (common)
            static_cast<uint16_t>(XCB_MOD_MASK_5),                 // NumLock on some layouts
            static_cast<uint16_t>(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2),
            static_cast<uint16_t>(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_5),
            static_cast<uint16_t>(XCB_MOD_MASK_2 | XCB_MOD_MASK_5),
            static_cast<uint16_t>(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2 | XCB_MOD_MASK_5),
        };
        for (uint16_t extra : lockMasks) {
            xcb_void_cookie_t cookie = xcb_grab_key_checked(connection, 1, root,
                                                            baseModifiers | extra, keycode,
                                                            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
            if (xcb_generic_error_t *error = xcb_request_check(connection, cookie)) {
                const uint8_t code = error->error_code;
                free(error);
                // BadAccess is the common case when the WM already owns the shortcut.
                qWarning("Failed to grab global hotkey on X11 (error=%u). The shortcut may be in use.", code);
            }
        }

        xcb_flush(connection);
        return true;
#else
        return false;
#endif
    }

    void unregisterHotkey() {
#ifdef Q_OS_WIN
        if (hotkeyId != 0) {
            UnregisterHotKey(nullptr, hotkeyId);
            hotkeyId = 0;
        }
#elif defined(Q_OS_LINUX)
        if (connection && keycode != 0) {
            const uint16_t baseModifiers = static_cast<uint16_t>(modifiers);
            const uint16_t lockMasks[] = {
                0,
                static_cast<uint16_t>(XCB_MOD_MASK_LOCK),
                static_cast<uint16_t>(XCB_MOD_MASK_2),
                static_cast<uint16_t>(XCB_MOD_MASK_5),
                static_cast<uint16_t>(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2),
                static_cast<uint16_t>(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_5),
                static_cast<uint16_t>(XCB_MOD_MASK_2 | XCB_MOD_MASK_5),
                static_cast<uint16_t>(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2 | XCB_MOD_MASK_5),
            };
            for (uint16_t extra : lockMasks) {
                xcb_ungrab_key(connection, keycode, root, baseModifiers | extra);
            }
            xcb_flush(connection);
            keycode = 0;
            modifiers = 0;
        }
#endif
    }

private:
    class EventFilter : public QAbstractNativeEventFilter {
    public:
        explicit EventFilter(Private *d) : d(d) {}

        bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override {
#ifdef Q_OS_WIN
            if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
                MSG *msg = static_cast<MSG *>(message);
                if (msg->message == WM_HOTKEY) {
                    emit d->q->hotkeyPressed();
                    return true;
                }
            }
#elif defined(Q_OS_LINUX)
            if (eventType == "xcb_generic_event_t") {
                xcb_generic_event_t *event = static_cast<xcb_generic_event_t *>(message);
                if ((event->response_type & ~0x80) == XCB_KEY_PRESS) {
                    xcb_key_press_event_t *kp = (xcb_key_press_event_t *)event;
                    if (kp->detail == d->keycode &&
                        (kp->state & d->modifiers) == d->modifiers) {
                        emit d->q->hotkeyPressed();
                        return true;
                    }
                }
            }
#endif
            return false;
        }

    private:
        Private *d;
    };

    HotkeyManager *q;
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

    friend class HotkeyManager;
};

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
    , d(new Private(this))
{
}

HotkeyManager::~HotkeyManager()
{
    delete d;
}

bool HotkeyManager::registerHotkey(const QKeySequence &keySequence)
{
    return d->registerHotkey(keySequence);
}

void HotkeyManager::unregisterHotkey()
{
    d->unregisterHotkey();
}
