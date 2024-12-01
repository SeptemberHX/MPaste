#include "HotkeyManager.h"
#include <QApplication>
#include <QAbstractNativeEventFilter>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <QX11Info>
#include <X11/Xlib.h>
#include <X11/keysym.h>
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
                emit manager->hotkeyPressed();
                return true;
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
    HotkeyEventFilter *eventFilter;
#ifdef Q_OS_WIN
    int hotkeyId;
#elif defined(Q_OS_LINUX)
    unsigned int keycode;
    unsigned int modifiers;
#endif
};

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->eventFilter = new HotkeyEventFilter(this);
    qApp->installNativeEventFilter(d->eventFilter);

#ifdef Q_OS_WIN
    d->hotkeyId = 0;
#endif
}

HotkeyManager::~HotkeyManager()
{
    unregisterHotkey();
    qApp->removeNativeEventFilter(d->eventFilter);
    delete d->eventFilter;
    delete d;
}

bool HotkeyManager::registerHotkey(const QKeySequence &keySequence)
{
    unregisterHotkey();

#ifdef Q_OS_WIN
    int modifiers = 0;
    int key = 0;

    // Parse the key sequence
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

    // Register the hotkey
    d->hotkeyId = 1; // You might want to generate unique IDs if registering multiple hotkeys
    return RegisterHotKey(nullptr, d->hotkeyId, modifiers, key);

#elif defined(Q_OS_LINUX)
    // X11 implementation would go here
    // This is a simplified version, you might need to add more code for X11
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
    // X11 unregister code would go here
#endif
}