# GNOME Wayland companion

`mpaste@mpaste.local` supplies GNOME 51 desktop integration over the user's session
bus (`org.mpaste.Gnome`, `/org/mpaste/Gnome`). It is required for MPaste on Ubuntu
26.10's GNOME Wayland session. This is not a generic KDE/wlroots backend.

The extension observes the compositor selection, reads MIME snapshots asynchronously,
and sends them only to the registered MPaste D-Bus connection. Registration verifies
the caller's process ID against D-Bus credentials. Clipboard changes invalidate pending
reads; capture is deferred until the selection notification has completed. Snapshots
are limited to 32 MiB in total (formats exceeding that budget are omitted). It does
not modify the clipboard while collecting history. Source application icons come from GNOME WindowTracker and the application's desktop entry. Both themed icon names and absolute icon files are supported; unrecognized applications still use the fallback icon.

The extension also owns the global accelerator, remembers the previous window, places
the MPaste panel at the bottom of its monitor's work area, and restores focus before
injecting a paste shortcut with a virtual keyboard. Paste waits for modifiers to be
released and cancels if focus changes or the original window disappears. Auto mode
recognizes common terminal application IDs and uses Ctrl+Shift+V there.

Install after building:

```sh
python3 tools/install_gnome_wayland.py --autostart
```

This installs the executable, translations, desktop launcher and extension under the
current user's data directory and optionally adds a login entry. Existing enabled
extensions are preserved. A **newly installed extension requires logout/login** on
GNOME 51: the old ReloadExtension API is no longer supported. Do not restart the
running Wayland compositor. After logging in, press Alt+Q or launch MPaste from the
application list. The desktop launcher uses `--show`; the autostart entry stays hidden.

To disable the companion: `gnome-extensions disable mpaste@mpaste.local`. To remove
login startup, remove `~/.config/autostart/MPaste.desktop`. MPaste needs the companion
re-enabled for background capture, hotkeys and automatic paste on this desktop.

The test driver under `tests/gnome-wayland/driver` is **only** copied into an isolated
test session. It must never be installed in the normal desktop session.

Before the companion is loaded, GNOME custom shortcuts can open/close the running MPaste preview. The dedicated system entry is at `org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/mpaste/`; its command invokes MPaste with `--toggle`. Existing custom shortcuts are preserved. When the companion registers the accelerator, this fallback binding is cleared to avoid competition.

Panel placement runs from the compositor `map` event before the first painted frame, including subsequent remaps. There is no delayed `window-created` timer, so the panel does not flash at GNOME’s default centered position. GNOME caches extension JavaScript for the session; upgrading this code requires a logout/login, not merely toggling the extension.

The `SourceIcon(u, as, s)` signal precedes the corresponding clipboard snapshot. Source metadata is captured when the selection changes, before the debounce and asynchronous MIME reads, so a later focus change does not replace the source application. Qt resolves the icon into a bounded 64-entry cache, and the existing item serialization embeds the pixmap. Metadata is never inserted into clipboard MIME formats. Older history with only a placeholder cannot be reliably backfilled because it did not store the source application identity.

Version 4 distinguishes `MPaste Preview` from the main `MPaste` panel and centers previews in the owning panel’s monitor work area at 980×760 logical pixels (clamped on small screens), matching the Windows Qt path. This compositor-side placement is necessary because Wayland ignores client `move()` requests.
