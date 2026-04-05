# Changelog

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
