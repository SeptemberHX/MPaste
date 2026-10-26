# Changelog

## Unreleased

- Aurora picture cards show their whole caption ("图片 · 1118 × 60"): the pill was sized to the text's width rounded to a whole pixel, often a fraction short, so the size was cut to "1118 × …".

## 0.8.2

Thumbnails keep their size: rich text no longer shrinks to half on a high-DPI screen, cards above 100% stay sharp, and an upgraded copy no longer leaves a duplicate card.

- Rich text no longer shrinks to half size after a search or a restart on a high-DPI screen when the card size is not 100% (as seen on Windows). A thumbnail read back from disk is reshaped to the current card size, and that drew a 2x image at its logical size, into the top-left quarter; a later save then kept the small one. It is now drawn by pixels, and rich-text thumbnails saved before are drawn again once from the HTML (header flag 0x4). Long pictures fitted whole on a card had the same flaw.
- Cards above 100% (Settings → card size, up to 150%) keep sharp thumbnails after a restart: a thumbnail read back was reshaped to the 100% size and then stretched over the larger card.
- Copying again something already in history, when the new copy carries more (rich text where plain text was saved, say), no longer leaves the old entry behind as a second card after a search or a restart; the entry also keeps its alias.

## 0.8.1

Rich text on Windows keeps its size after a search or a restart, and the Aurora light reaches across cards and windows.

- Card foot lines are shorter: "1600 × 1000" without a unit, "58 chars" in English (字符 in Chinese), so the app's name keeps more room.
- The favorites board is called "Favorites" in English (it read "Stared").
- Aurora cards and windows (preview, settings, OCR, details): the light from the top-left corner now reaches across to the right edge, still faintly lit there, instead of fading out halfway; it stays as shallow. The Aurora sample in Settings matches.
- Rich text on Windows no longer shrinks after a search or a restart. When the source app also put a picture of the selection on the clipboard (Word, Outlook, WPS...), the save that adds the formats read after the copy could store that picture as the card's thumbnail; reloaded, the card showed it, about half size, instead of the text. Rich text now always keeps its HTML render, and thumbnails saved the old way are drawn again once (header flag 0x2).
- README: new screenshots (taken in an isolated session with sample content), a Download section and an updated feature list.

## 0.8.0

A new look (Still and Aurora cards, Aurora windows), the quick paste stack beside the caret, the source app on every item, password managers' copies kept out of history, and a working `.deb` with releases built on GitHub Actions. History files move to `.mpaste v7`: older MPaste builds cannot read them. On GNOME Wayland the companion is now v10 (log out and back in once).

### Privacy

- Privacy (Settings → Privacy, on by default): what a password manager marks as secret is never recorded — KDE's `x-kde-passwordManagerHint`, and on Windows `ExcludeClipboardContentFromMonitorProcessing`, `Clipboard Viewer Ignore` or `CanIncludeInClipboardHistory` = 0 — nor written to the log. Copies from the apps listed there (Bitwarden, KeePassXC, KeePass and 1Password to begin with) are skipped entirely. Items already in history stay. On GNOME Wayland this needs companion v10 (log out and back in).

### Packaging and releases

- Debian package (`cpack -G DEB` → `mpaste_<version>_<arch>.deb`) brought up to date: Qt 6 dependencies read from the binary (it listed Qt 5), the GNOME companion and a login autostart entry included, the binary stripped and without the build's RUNPATH, the upstream changelog shipped. In a Wayland session MPaste now picks the Wayland platform itself, so `/usr/bin/MPaste` behaves like the per-user launcher.
- Build: dependencies come from the system and are checked at configure time, which names the package to install (`qt6-multimedia-dev`, `qt6-tools-dev`, `libxcb-keysyms1-dev`, `libxdo-dev`...) instead of failing later on a missing header; libxdo is found rather than assumed; `CXXFLAGS` and `-DCMAKE_CXX_FLAGS` are no longer overwritten.
- Releases are built on GitHub Actions: pushing a version tag builds the Windows zip and the Ubuntu 26.10 `.deb` and publishes them with that version's CHANGELOG notes; running the workflow by hand builds them without releasing.

### Quick paste stack

- New activation layout (Settings → Shortcuts → Opens): the hotkey opens a compact stack of the nine most recent items beside the text caret instead of the bottom panel. `1`–`9` paste, `Shift` pastes plain text, typing filters, `Tab` opens the full panel, `Esc` closes.
- Placement: GNOME Wayland via the companion (v8) from GNOME's input method or ubuntu-tools' kimpanel caret; Windows from the Win32 caret; otherwise at the pointer.
- Items now record the source application's name, shown as "app · time · type". The history format is `.mpaste v7`; v4–v6 files remain readable, but older MPaste builds cannot read v7 files (relevant when a sync folder is shared with an older build).

### Interface

- Settings: a roomier sidebar (wider, 40 px tabs that keep their height, the mark aligned with the tab icons); the close button of the Aurora windows is no longer clipped on Windows, where the native style padded it past its 32 px row.
- Cards: one-line header with the app's colour at a shared saturation and lightness, body tinted from it; smaller default card (248×264) and type; selection is a ring outside the card; favourites show a star in the header; hovering lifts the card.
- Text cards recognise colour values (swatch, hex and RGB), single web addresses (site first, path below) and one-line shell commands (monospace). File cards show the size and folder.
- Empty clipboard, empty favourites, filters and searches without results now say so.
- Top bar: steady layout while counts change; text that falls back to a font with a real medium weight when the UI font (e.g. Microsoft YaHei) has none.
- Touchpad swipes in either direction follow the fingers and glide on after release.
- Typing to search with an input method (fcitx, IBus) no longer loses the first key: the search box keeps the keyboard focus while the panel is open, folded away while empty, and the arrow keys, Space, Enter and Esc still act on the cards until something is typed.
- Tooltips have a solid ground in the dark theme.
- Preview, OCR, settings, details, alias and About windows take the Aurora look: a rounded window lit in the item's app colour, a header with the app's icon, pill buttons. The preview gains Paste, Copy, Read text and Save as, and Enter pastes, and its height follows the content (from a short strip for one line up to 85% of the screen, then it scrolls; the GNOME companion v9 keeps that size); the OCR result is editable and can be pasted directly; details lead with the content and the facts that matter; settings has a sidebar (General, Shortcuts, Sync, Text recognition, About), a segmented theme switch and pictured card-style choices.
- Fix: rich text copied from a light web page is readable on a dark card (and dark-page text on a light one): inline colours that read poorly on the card take the theme's text colour, a page's own white or black background is dropped, highlights and coloured text stay; earlier rich-text thumbnails are drawn again once.
- Fix: a long strip or a very tall page shows whole on its card instead of a close-up of its middle (older ones are redone when they come into view); rich text saved for the earlier card shape is drawn again at the current size instead of losing its first lines; the Still card's foot line shows the kind and size in full when there is room, the app's name giving way first.
- Fix: top-bar tabs and the search box sit centred in their capsule under a desktop Qt stylesheet (qt5ct/qt6ct), which used to stretch them past it and clip the chosen tab; the bar now sets its own sizes, as on Windows.
- Fix: the preview opens at the same height every time for the same item (it was measured against the previous preview's layout), and an open preview refits to new content.
- Fix: Save as no longer closes the moment it opens, dragging it no longer drags the panel along, and cancelling it no longer quits MPaste.
- Pictures on cards are no longer cut at the top and bottom: thumbnails are made in the shape of the card's picture area instead of being cropped twice, and the Aurora caption sits in a small pill rather than a shade over the picture's foot.
- Fix: rich text copied from WPS no longer shows its Windows clipboard header ("Version:0.9 StartHTML:…") in the card, the preview or when pasted; rich-text previews are no longer cut off at the card's top and left.
- Fix: starting MPaste while the clipboard still held something already saved (an image, typically) saved it again; captures that arrive before the history is indexed now wait for the index and are recognised.
- Preview window shows the source app, type, copy time and size instead of the stored file name.
- Settings window can be dragged and opens centred on GNOME Wayland (companion v7+).
- Fix: rich-text previews no longer render white text in light mode under a dark platform palette.
- Two card styles (Settings → General → Card style): **Still**, a neutral card with the content first and one quiet line of app, time and kind at its foot, pictures in a frame; and **Aurora**, the app's colour as a light in the card's corner under a header with its icon, pictures edge to edge. Both have light and dark versions; the coloured header band is gone. Cards are now 256 × 216, landscape. The board and kind tabs share one pill-shaped capsule with a hairline between the groups, and the search box and page indicator are pills too.
- Days are marked in the list: a thin rule with "Today", "Yesterday", the weekday or the date where the copy date changes (pinned items lead the list unmarked); only those boundaries widen to hold it, the other gaps stay as they were.
- Light theme cards: a deep app-colour header with white text over a clearly tinted body (was a pale pastel); yellows lean amber in both themes.
- Code shows in an inset block that keeps hyphenated words whole; rich-text links read on light cards; header bands get a hairline and the cards a lit top edge; the count reads "12 条"; hover-bar icons are centred and evenly sized.

## 0.7.5

### OCR

- Added manual OCR via right-click "Extract Text (OCR)" on Image and Office items.
- Windows backend: uses built-in Windows.Media.Ocr (PowerShell), tries all installed languages and picks the best result. Strips extra spaces between CJK characters.
- Baidu OCR API backend: supports `general_basic` endpoint with CHN_ENG mixed recognition (500 free calls/day). Configurable in settings.
- OCR result displayed in a frosted-glass dialog with copy-to-clipboard button, preserving original formatting (spaces, newlines).
- Results cached in `.ocr.json` sidecar files — re-clicking OCR shows cached result instantly.
- OCR text merged into the search index so recognized text is searchable.
- Auto OCR: optional setting to automatically OCR image items on capture (background, no UI popups). Shows "· OCR..." indicator on cards during processing.
- Settings UI: OCR backend selector (Windows Built-in / Baidu OCR API), API Key / Secret Key fields, auto-OCR toggle.

### MathType / Equation Support

- Detect MathType clipboard formats (including Qt's Windows MIME wrappers) and label cards with "· MathType" next to the timestamp.
- Extract MathML content as text preview with XML tag/annotation stripping, invisible Unicode operator removal, and UTF-16LE encoding support.
- Office cards without thumbnails (e.g. MathType) now fall back to text preview instead of showing infinite "Loading".
- Recognize OLE/vector-only clipboards (e.g. MathType with MetaFilePict + Embed Source but no standard text/image) in clipboard monitor.

### Fixes

- **Fingerprint stability**: removed contentType from hash so identical content produces the same fingerprint regardless of classification (e.g. Text vs RichText).
- **History cleanup**: implemented time-based retention using settings cutoff; disabled stale `maxSize=200` registry key and cleaned it up on startup.
- **Data race**: guarded `failedFullLoadPaths_` with QMutex between worker threads (read) and main thread (write).
- **inspect_mpaste.py**: fixed ContentType enum mapping to match `ContentType.h` (Link/RichText and Image/Color were swapped).
- **Corrupt MIME files**: warn once per file instead of flooding logs; set empty QMimeData on load failure so the loader is never retried.
- **OG image scaling**: cover-crop oversized OG images before saving as thumbnails to prevent aspect ratio distortion on reload.
- **Favorites sync**: wait for starred board's `deferredLoadCompleted` before reading fingerprints, so clipboard board gets fresh favorite markers.
- **SyncWatcher**: `suppressReloadUntil` now actually checked in `scheduleSyncReload`.
- **Link preview navigation stall**: debounce OG fetch (150ms) during rapid arrow-key scrolling; skip fetch for items that already have title/thumbnail.

### Startup Performance

- Stream first page of items during async disk scan so cards appear immediately instead of waiting for the full scan to complete. Files sorted by modification time (newest first).

### Architecture

- Extracted `ClipboardAppController` from `MPasteWidget` — owns ClipboardMonitor, ClipboardPasteController, CopySoundPlayer, OcrService, SyncWatcher. Sealed interface: no internal objects exposed.
- Split `MPasteWidget.cpp` (2300→842 lines) into `MPasteWidget.cpp`, `MPasteWidgetUI.cpp`, `MPasteWidgetKeys.cpp`.
- Split `ClipboardBoardService.cpp` (1476→855 lines) into core, IO, and async files with `ClipboardBoardServiceInternal.h` for shared helpers.
- Split `ScrollItemsWidgetMV.cpp` (2360→1581 lines) into core, thumbnails, and actions files.
- Deduplicated `rehydrateClipboardItem` into `ClipboardItem::rehydrate()` static method.
- Tightened rich text card line spacing from default to 1.1x.
- Documented core invariants: paint() no-IO rule, getImage() vs thumbnail() semantics, ensureMimeDataLoaded() restrictions, service file split routing.

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
