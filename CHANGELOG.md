# Changelog

## 0.7.4

Changes since v0.7.2.

### Performance

- Reduced wake-up latency by avoiding unnecessary `.mpaste` revalidation, moving visible thumbnail loading off the UI thread, and pre-rendering only the cards needed for the first frame.
- Improved long-idle responsiveness on Windows by keeping critical clipboard data warm in memory.
- Removed the obsolete thumbnail prefetch setting now that card pixmap caching handles first-frame rendering.

### Clipboard and Preview Reliability

- Improved content classification for Microsoft Office clipboard data and reclassified items after deferred MIME formats arrive, reducing Office and image misdetection.
- Fixed high-DPI thumbnail deserialization so cached previews stay sharp after reload.
- Improved rich-text thumbnail scaling and normalized extreme inline text colors for better readability in both dark and light themes.
- Fixed cases where cards could remain stuck in a loading state after deferred reclassification or failed thumbnail generation.
- Added a 10-second timeout to OpenGraph fetches and fixed a small polling leak in the preview/paste flow.

### Windows UI and Polish

- Embedded the application icon in Windows builds and improved icon fallback behavior for frameless and tool-style windows.
- Refined the glass-blur presentation across menus, the details dialog, settings, preview, and About windows with more consistent rounded corners.
- Restyled the About window as a themed glass dialog with drag support and a dedicated close button.
- Made dark mode the default theme and cleaned up the settings UI for the new blur styling.
- Fixed Windows autostart entries when MPaste is installed in a path containing spaces.
