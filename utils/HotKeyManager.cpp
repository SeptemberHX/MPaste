// input: 依赖对应头文件及其所需 Qt/标准库/同层组件实现。
// output: Windows/X11 热键注册，以及 GNOME Wayland 扩展热键桥接。
// pos: utils 层中的 HotKeyManager 实现文件。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
// HotKeyManager.cpp
#include "HotKeyManager.h"
#include <QApplication>
#include <QAbstractNativeEventFilter>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include "GnomeBridge.h"
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
        if (GnomeBridge::needsDesktopShortcut()) {
            QObject::connect(GnomeBridge::instance(), &GnomeBridge::hotkeyPressed,
                             q, &HotkeyManager::hotkeyPressed);
            return;
        }
        // Grab on Qt's own connection: X delivers the grabbed KeyPress to the
        // client that grabbed it, and the native event filter below only sees
        // events read from Qt's connection. A private one would never be read.
        if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
            connection = x11->connection();
        }
        if (!connection || xcb_connection_has_error(connection)) {
            qWarning("No X11 connection for the global hotkey");
            connection = nullptr;
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
        // The connection is Qt's; it is not ours to close.
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
        if (qtMods & Qt::MetaModifier)
            modifiers |= MOD_WIN;

        // Get the key
        int key = keySequence[0] & ~Qt::KeyboardModifierMask;

        // Register hotkey
        hotkeyId = 1;  // You might want to generate unique IDs
        return RegisterHotKey(nullptr, hotkeyId, modifiers, key);

#elif defined(Q_OS_LINUX)
        if (GnomeBridge::needsDesktopShortcut()) {
            if (keySequence.count() != 1) return false;
            const auto key = keySequence[0];
            QString accelerator;
            const auto mods = key.keyboardModifiers();
            if (mods & Qt::ControlModifier) accelerator += "<Control>";
            if (mods & Qt::AltModifier) accelerator += "<Alt>";
            if (mods & Qt::ShiftModifier) accelerator += "<Shift>";
            if (mods & Qt::MetaModifier) accelerator += "<Super>";
            QString keyName = QKeySequence(key.key()).toString(QKeySequence::PortableText);
            if (key.key() >= Qt::Key_A && key.key() <= Qt::Key_Z) keyName = keyName.toLower();
            switch (key.key()) {
            case Qt::Key_Space: keyName = "space"; break;
            case Qt::Key_Backspace: keyName = "BackSpace"; break;
            case Qt::Key_PageUp: keyName = "Page_Up"; break;
            case Qt::Key_PageDown: keyName = "Page_Down"; break;
            case Qt::Key_Enter: keyName = "KP_Enter"; break;
            case Qt::Key_Escape: keyName = "Escape"; break;
            case Qt::Key_Insert: keyName = "Insert"; break;
            case Qt::Key_Delete: keyName = "Delete"; break;
            default: break;
            }
            accelerator += keyName;
            return GnomeBridge::instance()->registerShortcut(accelerator);
        }
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
        xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(keySymbols, key);
        if (!keycodes) {
            return false;
        }

        keycode = keycodes[0];
        free(keycodes);

        // Register global hotkey. A passive grab matches the modifier state
        // exactly, so also grab the NumLock / CapsLock variants.
        bool grabbed = true;
        for (uint32_t lockMask : kLockMasks) {
            xcb_void_cookie_t cookie = xcb_grab_key_checked(connection, 1, root,
                     modifiers | lockMask, keycode,
                     XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
            if (xcb_generic_error_t *error = xcb_request_check(connection, cookie)) {
                // BadAccess: another client already owns this combination.
                free(error);
                grabbed = false;
            }
        }
        if (!grabbed) {
            for (uint32_t lockMask : kLockMasks) {
                xcb_ungrab_key(connection, keycode, root, modifiers | lockMask);
            }
            xcb_flush(connection);
            keycode = 0;
            modifiers = 0;
            return false;
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
        if (GnomeBridge::needsDesktopShortcut()) {
            GnomeBridge::instance()->unregisterShortcut();
            return;
        }
        if (connection && keycode != 0) {
            for (uint32_t lockMask : kLockMasks) {
                xcb_ungrab_key(connection, keycode, root, modifiers | lockMask);
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
                    // Compare the real modifiers only; NumLock / CapsLock vary.
                    if (d->keycode != 0 && kp->detail == d->keycode &&
                        (kp->state & kHotkeyModifierMask) == d->modifiers) {
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
    // Shift, Control, Alt (Mod1) and Super (Mod4): what a hotkey is made of.
    static constexpr uint32_t kHotkeyModifierMask =
        XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_4;
    // CapsLock (Lock) and NumLock (Mod2), in every combination.
    static constexpr uint32_t kLockMasks[4] = {
        0, XCB_MOD_MASK_LOCK, XCB_MOD_MASK_2, XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2};
    xcb_connection_t *connection = nullptr;
    xcb_window_t root = 0;
    xcb_key_symbols_t *keySymbols = nullptr;
    uint32_t keycode = 0;
    uint32_t modifiers = 0;
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
