#include <QtGlobal>
#ifdef Q_OS_LINUX
// input: GNOME companion D-Bus service and session ownership notifications.
// output: Directed clipboard/source-icon delivery and GNOME shortcuts with a launcher fallback.
// pos: Linux Wayland integration, not used on Windows or X11.
// update: Keep utils/README.md and extension.js protocol in sync.
#include "GnomeBridge.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QTimer>
#include <QProcess>
#include <QGuiApplication>
#include <QIcon>
#include <QFileInfo>

namespace {
constexpr auto service = "org.mpaste.Gnome";
constexpr auto path = "/org/mpaste/Gnome";
constexpr auto iface = "org.mpaste.Gnome";
constexpr auto shortcutSchema = "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/mpaste/";

QString settingsCommand(const QStringList &arguments, bool *ok = nullptr) {
    QProcess process;
    process.start(QStringLiteral("gsettings"), arguments);
    const bool success = process.waitForFinished(3000) && process.exitCode() == 0;
    if (ok) *ok = success;
    if (!success) qWarning() << "[gnome-shortcut]" << process.readAllStandardError();
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}
QString variantString(QString value) {
    value.replace("\\", "\\\\");
    value.replace("'", "\\'");
    return "'" + value + "'";
}
bool desktopShortcut(const QString &accelerator) {
    bool ok = false;
    const QString schema = QString::fromLatin1(shortcutSchema);
    QString command = QCoreApplication::applicationFilePath();
    command.replace("'", "'\\''");
    command = "'" + command + "' --toggle";
    settingsCommand({"set", schema, "name", "'MPaste'"}, &ok);
    if (!ok) return false;
    settingsCommand({"set", schema, "command", variantString(command)}, &ok);
    if (!ok) return false;
    settingsCommand({"set", schema, "binding", variantString(accelerator)}, &ok);
    if (!ok || accelerator.isEmpty()) return ok;
    const QString parent = "org.gnome.settings-daemon.plugins.media-keys";
    QString bindings = settingsCommand({"get", parent, "custom-keybindings"}, &ok);
    if (!ok) return false;
    const QString entry = "'/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/mpaste/'";
    if (!bindings.contains(entry)) {
        if (bindings.startsWith("@as ")) bindings.remove(0, 4);
        if (!bindings.startsWith('[') || !bindings.endsWith(']')) return false;
        bindings.chop(1);
        if (bindings.size() > 1) bindings += ", ";
        bindings += entry + "]";
        settingsCommand({"set", parent, "custom-keybindings", bindings}, &ok);
    }
    return ok;
}
}
bool GnomeBridge::isWayland() {
    return qEnvironmentVariable("XDG_SESSION_TYPE") == QStringLiteral("wayland");
}
bool GnomeBridge::needsDesktopShortcut() {
    return isWayland() || (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains("GNOME", Qt::CaseInsensitive));
}
GnomeBridge *GnomeBridge::instance() {
    static auto *bridge = new GnomeBridge;
    return bridge;
}
GnomeBridge::GnomeBridge() : QObject(qApp) {
    auto bus = QDBusConnection::sessionBus();
    bus.connect(service, path, iface, "ClipboardChanged", this,
                SLOT(receiveClipboard(QVariantMap,uint)));
    bus.connect(service, path, iface, "SourceIcon", this,
                SLOT(receiveSourceIcon(uint,QStringList,QString)));
    // Companion v8+; older companions never send it and items keep no app name.
    bus.connect(service, path, iface, "SourceApp", this,
                SLOT(receiveSourceApp(uint,QString)));
    bus.connect(service, path, iface, "HotkeyPressed", this, SLOT(receiveHotkey()));
    auto *watcher = new QDBusServiceWatcher(service, bus,
        QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &GnomeBridge::reconnect);
    QTimer::singleShot(0, this, &GnomeBridge::reconnect);
}
QDBusMessage GnomeBridge::call(const QString &method, const QVariantList &args) {
    auto message = QDBusMessage::createMethodCall(service, path, iface, method);
    message.setArguments(args);
    const auto reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 1500);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning() << "[gnome-bridge]" << method << reply.errorMessage();
    return reply;
}
void GnomeBridge::reconnect() {
    if (!accelerator_.isEmpty()) registerShortcut(accelerator_);
}
bool GnomeBridge::registerShortcut(const QString &accelerator) {
    accelerator_ = accelerator;
    const auto registration = call("Register", {uint(QCoreApplication::applicationPid())});
    if (registration.type() == QDBusMessage::ErrorMessage) {
        qInfo() << "[gnome-shortcut] using GNOME Settings launcher until the companion loads";
        return desktopShortcut(accelerator);
    }
    desktopShortcut(QString()); // Release the temporary compositor binding before grabbing it.
    auto reply = call("BindShortcut", {accelerator});
    return reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty()
        && reply.arguments().first().toBool();
}
void GnomeBridge::unregisterShortcut() {
    accelerator_.clear();
    desktopShortcut(QString());
    call("BindShortcut", {QString()});
}
void GnomeBridge::receiveClipboard(const QVariantMap &formats, uint window) {
    emit clipboardReceived(formats, window);
}
QPixmap GnomeBridge::sourceIcon(uint window) const {
    if (const auto *icon = sourceIcons_.object(window)) return *icon;
    return QPixmap(QStringLiteral(":/resources/resources/unknown.svg"));
}
void GnomeBridge::receiveSourceIcon(uint window, const QStringList &names, const QString &filePath) {
    QPixmap pixmap;
    if (QFileInfo(filePath).isAbsolute() && QFileInfo(filePath).isFile())
        pixmap = QIcon(filePath).pixmap(64, 64);
    for (const auto &name : names) {
        if (!pixmap.isNull()) break;
        pixmap = QIcon::fromTheme(name).pixmap(64, 64);
    }
    if (pixmap.isNull()) sourceIcons_.remove(window);
    else sourceIcons_.insert(window, new QPixmap(pixmap));
}
QString GnomeBridge::sourceName(uint window) const {
    if (const auto *name = sourceNames_.object(window)) return *name;
    return QString();
}
void GnomeBridge::receiveSourceApp(uint window, const QString &name) {
    if (name.isEmpty()) sourceNames_.remove(window);
    else sourceNames_.insert(window, new QString(name));
}
void GnomeBridge::receiveHotkey() { emit hotkeyPressed(); }

#endif
