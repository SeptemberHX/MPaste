// HotKeyManager.cpp
#include "HotKeyManager.h"
#include <QApplication>
#include <QAbstractNativeEventFilter>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
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
        // Initialize XCB connection
        connection = xcb_connect(nullptr, nullptr);
        if (xcb_connection_has_error(connection)) {
            qWarning("Failed to connect to X server");
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
        if (connection) {
            xcb_disconnect(connection);
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

        // Get keycode
        int key = keySequence[0] & ~Qt::KeyboardModifierMask;
        xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(keySymbols, key);
        if (!keycodes) {
            return false;
        }

        keycode = keycodes[0];
        free(keycodes);

        // Register global hotkey
        xcb_grab_key(connection, 1, root,
                     modifiers, keycode,
                     XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

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
            xcb_ungrab_key(connection, keycode, root, modifiers);
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