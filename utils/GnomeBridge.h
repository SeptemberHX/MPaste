#include <QtGlobal>
#ifdef Q_OS_LINUX
// input: Qt DBus and the MPaste GNOME Shell companion extension.
// output: Wayland clipboard/window integration, source app icon and name caches, GNOME shortcuts.
// pos: Linux desktop bridge; all calls run on the GUI thread.
// update: Keep utils/README.md and the extension protocol in sync.
#pragma once
#include <QObject>
#include <QVariantMap>
#include <QDBusMessage>
#include <QCache>
#include <QPixmap>
#include <QStringList>

class GnomeBridge : public QObject {
    Q_OBJECT
public:
    static bool isWayland();
    static bool needsDesktopShortcut();
    static GnomeBridge *instance();
    QDBusMessage call(const QString &method, const QVariantList &args = {});
    bool registerShortcut(const QString &accelerator);
    void unregisterShortcut();
    QPixmap sourceIcon(uint window) const;
    QString sourceName(uint window) const;
signals:
    void hotkeyPressed();
    void clipboardReceived(const QVariantMap &formats, uint window);
private slots:
    void reconnect();
    void receiveClipboard(const QVariantMap &formats, uint window);
    void receiveHotkey();
    void receiveSourceIcon(uint window, const QStringList &names, const QString &path);
    void receiveSourceApp(uint window, const QString &name);
private:
    GnomeBridge();
    QString accelerator_;
    QCache<uint, QPixmap> sourceIcons_{64};
    QCache<uint, QString> sourceNames_{64};
};

#endif
