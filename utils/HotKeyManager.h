#ifndef MPASTE_HOTKEYMANAGER_H
#define MPASTE_HOTKEYMANAGER_H

#include <QObject>
#include <QKeySequence>

class HotkeyManager : public QObject {
    Q_OBJECT

public:
    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager();

    bool registerHotkey(const QKeySequence &keySequence);
    void unregisterHotkey();

    signals:
        void hotkeyPressed();

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result);

private:
    class Private;
    Private *d;
};

#endif // MPASTE_HOTKEYMANAGER_H