# Changelog

## 0.7.4

### Performance

- Wake-up latency reduced from ~6s to <50ms by skipping full `.mpaste` format revalidation during incremental sync (only new files are checked now).
- Visible thumbnail loading moved from synchronous disk I/O to async background requests, unblocking the showEvent path entirely.
- First-frame card rendering pre-warmed after the initial data batch loads, eliminating the ~1s cold-cache stall on first wake.
- Pre-render count uses screen width as fallback when the viewport is hidden, so startup cache warming covers the right number of cards.
- Loading overlay shown immediately on early wake when data is still loading from disk.

### Clipboard and Preview

- Fixed cards stuck on "Loading" placeholder after deferred MIME reclassification (e.g. RichText → Color) by syncing preview state after `reclassifyContentType`.
- Fixed thumbnail generation failures silently leaving cards in loading state — now properly marked as missing to fall back to text display.
- Office items from Word now show selectable rich text or plain text in Space preview instead of an unselectable image snapshot; image-only items (e.g. PPT shapes) still show the image.
- Card text preview limit raised from 200 to 500 characters for better readability of long text items.
- Added 10-second network timeout to OpenGraph fetcher to prevent indefinite hangs.
- Fixed small memory leak: polling counter in paste controller now uses stack variable instead of heap allocation.

### Windows UI

- Embedded multi-size `.ico` into the Windows executable so the app icon appears in Task Manager, Alt+Tab, and the taskbar.
- Fixed `setWindowIcon` using `QIcon::fromTheme` (Linux-only API) — now uses the embedded SVG resource on all platforms.
- Aligned painted border radius to DWM `DWMWCP_ROUND` (~8px) across all blur windows: main panel, preview, details, settings, context menus, and page selector.
- Removed redundant QFrame card layer from preview dialog that caused a visible double-window effect.
- Restyled About window as a frameless glass dialog with blur, drag-to-move, close button, and dark/light theme support.
- Settings dialog: removed stacking semi-transparent backgrounds that appeared as opaque dark blocks; now uses a single transparent pane layer.
- Removed unused "thumbnail prefetch" setting from the UI.
- Default theme changed to Dark.

### Tooling

- Updated `tools/inspect_mpaste.py` to handle Qt 6.8 QPixmap PNG serialization format (was broken on all V6 files).
- Added `CLAUDE.md` with item inspection workflow guidance.

## 0.7.3

### Performance

- Reduced idle wake-up overhead and improved clipboard monitoring responsiveness.
- Added timing instrumentation to the wake/show path for performance diagnostics.

### Clipboard

- Improved Office content classification for items with OLE-backed HTML (e.g. Word "Embed Source" format).
- Fixed high-DPI thumbnail device pixel ratio deserialization so cached previews render crisply after reload.
- Added deferred content reclassification when late-arriving MIME formats change the item type.

### UI

- Improved dark-mode rich text card rendering with better color normalization.
- Fixed icon fallback for frameless tool windows on Windows.
- Fixed autostart registry path when the executable path contains spaces.

## 0.7.2

### UI

- Added glass blur effect to context menus and the details dialog.
- Improved glass blur presentation consistency across all dialog windows.
