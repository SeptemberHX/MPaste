// input: GNOME Shell selection, keybinding and window APIs; authenticated session client.
// output: Directed clipboard snapshots and desktop actions; panel follows the pointer's monitor before its first frame; dialogs are centred on map.
// pos: GNOME 51 Wayland companion. No clipboard data is written to disk here.
// update: Keep utils/GnomeBridge and extensions/README.md in sync.
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import Clutter from 'gi://Clutter';
import Mtk from 'gi://Mtk';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const NAME = 'org.mpaste.Gnome';
const PATH = '/org/mpaste/Gnome';
const XML = `<node><interface name="${NAME}">
<method name="Register"><arg type="u" direction="in" name="pid"/></method>
<method name="BindShortcut"><arg type="s" direction="in" name="key"/><arg type="b" direction="out" name="ok"/></method>
<method name="CurrentWindow"><arg type="u" direction="out" name="window"/></method>
<method name="PreviousWindow"><arg type="u" direction="out" name="window"/></method>
<method name="Activate"><arg type="u" direction="in" name="window"/></method>
<method name="ShowPanel"/>
<method name="Paste"><arg type="u" direction="in" name="mode"/></method>
<signal name="HotkeyPressed"/>
<signal name="SourceIcon"><arg type="u" name="window"/><arg type="as" name="names"/><arg type="s" name="path"/></signal>
<signal name="SourceApp"><arg type="u" name="window"/><arg type="s" name="name"/></signal>
<signal name="ClipboardChanged"><arg type="a{sv}" name="formats"/><arg type="u" name="window"/></signal>
</interface></node>`;
const IMAGE_PREFERENCE = ['image/png', 'image/tiff', 'image/bmp', 'image/webp', 'image/jpeg'];
// MPaste's own windows besides the panel and preview (settings, about, details,
// rename): popups and tooltips are placed by their parent, not here.
const DIALOG_TYPES = new Set([Meta.WindowType.NORMAL, Meta.WindowType.DIALOG,
    Meta.WindowType.MODAL_DIALOG, Meta.WindowType.UTILITY]);

export default class MPasteExtension extends Extension {
    enable() {
        this._client = null;
        this._pid = 0;
        this._generation = 0;
        this._captureTimer = 0;
        this._sources = new Set();
        this._clipboard = St.Clipboard.get_default();
        this._selection = global.display.get_selection();
        this._selectionId = this._selection.connect('owner-changed', (_s, type) => {
            if (type === Meta.SelectionType.SELECTION_CLIPBOARD) this._queueCapture();
        });
        this._keyboard = global.stage.context.get_backend().get_default_seat()
            .create_virtual_device(Clutter.InputDeviceType.KEYBOARD_DEVICE);
        this._acceleratorId = global.display.connect('accelerator-activated', (_d, action) => {
            if (action !== this._action || !this._client) return;
            const focus = global.display.focus_window;
            if (focus?.get_pid() !== this._pid) {
                this._previous = focus;
                // Read the caret now: once MPaste takes focus it is gone.
                this._anchor = this._caretAnchor(focus);
            }
            this._emit('HotkeyPressed', null);
        });
        // map runs when the committed surface and its real size are available,
        // before the compositor paints it. Also runs on subsequent remaps.
        this._mapId = global.window_manager.connect_after('map', (_wm, actor) => {
            const window = actor.get_meta_window();
            if (window.get_pid() !== this._pid) return;
            if (window.get_title() === 'MPaste') this._showPanel(window);
            else if (window.get_title() === 'MPaste Preview') this._placePreview(window);
            else if (window.get_title() === 'MPaste Quick') this._placeQuick(window);
            else if (DIALOG_TYPES.has(window.get_window_type())) this._centerDialog(window);
        });
        this._dbusId = Gio.DBus.session.register_object(PATH,
            Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0],
            this._method.bind(this), null, null);
        this._owner = Gio.bus_own_name_on_connection(Gio.DBus.session, NAME,
            Gio.BusNameOwnerFlags.NONE, null, null);
    }
    disable() {
        this._generation++;
        this._bind('');
        for (const id of this._sources) GLib.source_remove(id);
        this._sources.clear();
        this._selection.disconnect(this._selectionId);
        global.display.disconnect(this._acceleratorId);
        global.window_manager.disconnect(this._mapId);
        if (this._watch) Gio.bus_unwatch_name(this._watch);
        Gio.DBus.session.unregister_object(this._dbusId);
        Gio.bus_unown_name(this._owner);
        this._client = null;
        this._keyboard = null;
        this._previous = null;
    }
    _later(ms, fn) {
        const id = GLib.timeout_add(GLib.PRIORITY_DEFAULT, ms, () => {
            this._sources.delete(id);
            fn();
            return GLib.SOURCE_REMOVE;
        });
        this._sources.add(id);
        return id;
    }
    _emit(signal, value) {
        if (this._client)
            Gio.DBus.session.emit_signal(this._client, PATH, NAME, signal, value);
    }
    _method(_connection, sender, _path, _iface, method, params, invocation) {
        try {
            const args = params.deepUnpack();
            if (method === 'Register') {
                // Resolve the caller's actual PID instead of trusting its supplied PID.
                const reply = Gio.DBus.session.call_sync('org.freedesktop.DBus',
                    '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                    'GetConnectionUnixProcessID', new GLib.Variant('(s)', [sender]),
                    new GLib.VariantType('(u)'), Gio.DBusCallFlags.NONE, 1000, null);
                const [pid] = reply.deepUnpack();
                if (pid !== args[0]) throw new Error('Caller PID mismatch');
                if (this._client && this._client !== sender) throw new Error('MPaste client already connected');
                if (this._watch) Gio.bus_unwatch_name(this._watch);
                this._client = sender;
                this._pid = pid;
                this._watch = Gio.bus_watch_name_on_connection(Gio.DBus.session, sender,
                    Gio.BusNameWatcherFlags.NONE, null, () => {
                        this._client = null;
                        this._generation++;
                        this._bind('');
                    });
                invocation.return_value(null);
                this._capture();
                return;
            }
            if (sender !== this._client) throw new Error('Register MPaste first');
            if (method === 'BindShortcut') {
                invocation.return_value(new GLib.Variant('(b)', [this._bind(args[0])]));
                return;
            }
            if (method === 'CurrentWindow') {
                const focus = global.display.focus_window;
                if (focus?.get_pid() !== this._pid) this._previous = focus;
                invocation.return_value(new GLib.Variant('(u)', [focus?.get_stable_sequence() ?? 0]));
                return;
            }
            if (method === 'PreviousWindow') {
                const focus = global.display.focus_window;
                if (focus?.get_pid() !== this._pid) this._previous = focus;
                invocation.return_value(new GLib.Variant('(u)', [this._previous?.get_stable_sequence() ?? 0]));
                return;
            }
            if (method === 'Activate') {
                const window = global.get_window_actors().map(a => a.meta_window)
                    .find(w => w.get_stable_sequence() === args[0]);
                if (window) Main.activateWindow(window);
            } else if (method === 'ShowPanel') {
                this._showPanel();
            } else if (method === 'Paste') {
                this._paste(args[0]);
            }
            invocation.return_value(null);
        } catch (e) {
            invocation.return_dbus_error(`${NAME}.Error`, e.message);
        }
    }
    _bind(key) {
        if (this._action) {
            Main.wm.allowKeybinding(Meta.external_binding_name_for_action(this._action), Shell.ActionMode.NONE);
            global.display.ungrab_accelerator(this._action);
            this._action = 0;
        }
        if (!key) return true;
        this._action = global.display.grab_accelerator(key, Meta.KeyBindingFlags.NONE);
        if (!this._action) return false;
        Main.wm.allowKeybinding(Meta.external_binding_name_for_action(this._action), Shell.ActionMode.NORMAL);
        return true;
    }
    _showPanel(panel = null) {
        panel ??= global.get_window_actors().map(a => a.meta_window)
            .find(w => w.get_pid() === this._pid && w.get_title() === 'MPaste');
        if (!panel) return;
        // Placement follows the pointer; _previous remains the paste target.
        const [x, y] = global.get_pointer();
        const pointerMonitor = Main.layoutManager.monitors.findIndex(m =>
            x >= m.x && x < m.x + m.width && y >= m.y && y < m.y + m.height);
        const monitor = pointerMonitor >= 0 ? pointerMonitor : Main.layoutManager.primaryIndex;
        const area = Main.layoutManager.getWorkAreaForMonitor(monitor);
        const frame = panel.get_frame_rect();
        panel.move_to_monitor(monitor);
        panel.move_resize_frame(false, area.x, area.y + area.height - frame.height,
            area.width, frame.height);
        Main.activateWindow(panel);
    }
    _caretAnchor(focus) {
        // The text caret of the focused window in stage coordinates, else null.
        // GNOME's own input method knows it for text-input clients; fcitx clients
        // report it to the kimpanel, which ubuntu-tools exposes. Both keep stale
        // values, so only a rect inside the focused window counts.
        const candidates = [];
        try {
            const im = Main.inputMethod;
            if (im?._currentFocus && im._cursorRect) candidates.push(im._cursorRect);
        } catch (_e) {}
        try {
            const tools = Main.extensionManager.lookup('ubuntu-tools@ubuntu-tools');
            const rect = tools?.stateObj?._impls?.get('kimpanel')?.caretRect?.();
            if (rect) candidates.push(rect);
        } catch (_e) {}
        const frame = focus?.get_frame_rect();
        for (const r of candidates) {
            const x = r.x, y = r.y, width = r.width ?? 0, height = r.height ?? 0;
            if (!frame || !Number.isFinite(x) || !Number.isFinite(y) || (x === 0 && y === 0)) continue;
            if (x >= frame.x && x <= frame.x + frame.width && y >= frame.y && y <= frame.y + frame.height)
                return {x, y, width: Math.max(1, width), height: Math.max(1, height)};
        }
        return null;
    }
    _placeQuick(window) {
        // Below the caret, else above, else beside it, inside the work area of its
        // monitor; the pointer stands in when no caret is known.
        let anchor = this._anchor;
        if (!anchor) {
            const [x, y] = global.get_pointer();
            anchor = {x, y, width: 1, height: 1};
        }
        const monitor = global.display.get_monitor_index_for_rect(new Mtk.Rectangle({
            x: anchor.x, y: anchor.y, width: anchor.width, height: anchor.height}));
        const area = Main.layoutManager.getWorkAreaForMonitor(monitor >= 0 ? monitor : Main.layoutManager.primaryIndex);
        const frame = window.get_frame_rect();
        const gap = 6;
        const below = anchor.y + anchor.height + gap;
        const above = anchor.y - gap - frame.height;
        let x = Math.max(area.x, Math.min(anchor.x, area.x + area.width - frame.width));
        let y;
        if (below + frame.height <= area.y + area.height) {
            y = below;
        } else if (above >= area.y) {
            y = above;
        } else {
            // Neither fits: beside the caret, never over it.
            y = Math.max(area.y, Math.min(anchor.y + anchor.height / 2 - frame.height / 3,
                area.y + area.height - frame.height));
            const right = anchor.x + anchor.width + 2 * gap;
            x = right + frame.width <= area.x + area.width ? right
                : Math.max(area.x, anchor.x - 2 * gap - frame.width);
        }
        y = Math.round(y);
        window.move_to_monitor(monitor >= 0 ? monitor : Main.layoutManager.primaryIndex);
        window.move_frame(false, x, y);
        Main.activateWindow(window);
    }
    _dialogMonitor(window) {
        // The owning panel's monitor, else the paste target's, else the current one.
        const parent = window.get_transient_for();
        return parent?.get_monitor() ?? this._previous?.get_monitor()
            ?? global.display.get_current_monitor();
    }
    _centerDialog(window) {
        // Qt centres its dialogs with move(), which Wayland ignores; keep the
        // window's own size and centre it in the work area, as on Windows.
        const monitor = this._dialogMonitor(window);
        const area = Main.layoutManager.getWorkAreaForMonitor(monitor);
        const frame = window.get_frame_rect();
        window.move_to_monitor(monitor);
        window.move_frame(false, area.x + Math.max(0, Math.floor((area.width - frame.width) / 2)),
            area.y + Math.max(0, Math.floor((area.height - frame.height) / 2)));
    }
    _placePreview(window) {
        // Match the Qt/Windows preview: 980x760 logical pixels, centered in the
        // owning panel's work area. Wayland clients cannot position top-levels.
        const monitor = this._dialogMonitor(window);
        const area = Main.layoutManager.getWorkAreaForMonitor(monitor);
        const width = Math.min(980, Math.max(1, area.width - 32));
        const height = Math.min(760, Math.max(1, area.height - 32));
        window.move_to_monitor(monitor);
        window.move_resize_frame(false, area.x + Math.floor((area.width - width) / 2),
            area.y + Math.floor((area.height - height) / 2), width, height);
        Main.activateWindow(window);
    }
    _paste(mode) {
        const target = this._previous;
        if (!target || !global.get_window_actors().some(a => a.meta_window === target)) return;
        Main.activateWindow(target);
        let tries = 0;
        const send = () => {
            // Never paste into a window that gained focus while we were waiting.
            if (global.display.focus_window !== target) return;
            const modifiers = global.get_pointer()[2];
            const held = Clutter.ModifierType.SHIFT_MASK | Clutter.ModifierType.CONTROL_MASK |
                Clutter.ModifierType.MOD1_MASK | Clutter.ModifierType.SUPER_MASK;
            if (modifiers & held) {
                if (++tries < 100) this._later(20, send);
                return;
            }
            const app = Shell.WindowTracker.get_default().get_window_app(target);
            const terminal = /terminal|console|ptyxis|kgx|konsole|tilix|alacritty|kitty|wezterm|foot/i
                .test(`${app?.get_id() ?? ''} ${target.get_wm_class() ?? ''}`);
            const ctrlShift = mode === 3 || (mode === 0 && terminal);
            const keys = mode === 2 ? [Clutter.KEY_Shift_L, Clutter.KEY_Insert]
                : mode === 4 ? [Clutter.KEY_Alt_L, Clutter.KEY_Insert]
                : ctrlShift ? [Clutter.KEY_Control_L, Clutter.KEY_Shift_L, Clutter.KEY_v]
                : [Clutter.KEY_Control_L, Clutter.KEY_v];
            const time = GLib.get_monotonic_time();
            for (const key of keys) this._keyboard.notify_keyval(time, key, Clutter.KeyState.PRESSED);
            for (const key of keys.reverse()) this._keyboard.notify_keyval(time, key, Clutter.KeyState.RELEASED);
        };
        this._later(120, send);
    }
    _queueCapture() {
        // St updates its selection view in the same owner-changed dispatch.
        // Read on a later turn, not from inside that dispatch; invalidate old reads now.
        this._generation++;
        if (this._captureTimer) {
            GLib.source_remove(this._captureTimer);
            this._sources.delete(this._captureTimer);
        }
        const source = this._clipboardSource();
        this._captureTimer = this._later(50, () => {
            this._captureTimer = 0;
            this._capture(source);
        });
    }
    _clipboardSource() {
        const window = global.display.focus_window;
        const app = window ? Shell.WindowTracker.get_default().get_window_app(window) : null;
        const icon = app?.get_app_info()?.get_icon();
        const names = icon instanceof Gio.ThemedIcon ? icon.get_names() : [];
        const path = icon instanceof Gio.FileIcon ? icon.get_file().get_path() : '';
        // A window without a desktop entry gets an app named after its title
        // (a document name); its WM class says which program it is.
        const name = (app && !app.is_window_backed() ? app.get_name() : window?.get_wm_class()) ?? '';
        return {window: window?.get_stable_sequence() ?? 0,
            own: window?.get_pid() === this._pid, names, path: path ?? '', name};
    }
    _capture(source = this._clipboardSource()) {
        const generation = ++this._generation;
        if (!this._client) return;
        if (source.own) return;
        const window = source.window;
        let formats = this._clipboard.get_mimetypes(St.ClipboardType.CLIPBOARD);
        // Toolkits re-encode one image into every writer format (Qt offers xpm, xbm,
        // wbmp, tiff... before png). Read only the best one so lossy or monochrome
        // encodings never consume the size budget ahead of the real image.
        const image = IMAGE_PREFERENCE.find(f => formats.includes(f));
        if (image) formats = [image, ...formats.filter(f => !f.startsWith('image/'))];
        const data = {};
        let size = 0;
        const next = () => {
            if (generation !== this._generation || !this._client) return;
            const format = formats.shift();
            if (!format) {
                if (Object.keys(data).length) {
                    // Separate metadata keeps the clipboard protocol backward compatible
                    // and never inserts app information into the pasted MIME payload.
                    this._emit('SourceIcon', new GLib.Variant('(uass)', [window, source.names, source.path]));
                    this._emit('SourceApp', new GLib.Variant('(us)', [window, source.name]));
                    this._emit('ClipboardChanged', new GLib.Variant('(a{sv}u)', [data, window]));
                }
                return;
            }
            // Skip X11 protocol targets; preserve application MIME formats verbatim.
            if (!format.includes('/')) { next(); return; }
            this._clipboard.get_content(St.ClipboardType.CLIPBOARD, format, (_c, bytes) => {
                if (generation !== this._generation) return;
                const array = bytes instanceof GLib.Bytes ? bytes.toArray() : bytes;
                if (array?.length) {
                    size += array.length;
                    if (size <= 32 * 1024 * 1024) data[format] = new GLib.Variant('ay', array);
                }
                next();
            });
        };
        next();
    }
}
