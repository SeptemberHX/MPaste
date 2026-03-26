<!--
input: 依赖 utils 目录的实际结构、工具职责与近期实现变化。
output: 提供 utils 目录的中文说明与文件清单。
pos: 工具层目录说明文档。
update: 修改本目录文件时，同步更新本 README。
-->

# utils

> `utils` 目录承载跨模块复用的系统能力，包括剪贴板监听、热键、设置、平台适配和单实例控制。

## 目录职责

- 监听系统剪贴板并将原始事件整理成应用可消费的数据。
- 管理全局快捷键、应用设置以及平台相关行为。
- 提供链接元数据抓取、单实例控制等通用基础能力。

## 文件说明

- `ClipboardMonitor.h` / `ClipboardMonitor.cpp`：系统剪贴板监听与采集逻辑。
- `HotKeyManager.h` / `HotKeyManager.cpp`：全局快捷键注册与管理。
- `MPasteSettings.h` / `MPasteSettings.cpp`：运行配置、持久化设置与默认值管理。
- `ThemeManager.h` / `ThemeManager.cpp`????????????????????
- `IconResolver.h` / `IconResolver.cpp`???????/???????
- `OpenGraphFetcher.h` / `OpenGraphFetcher.cpp`：抓取网页 Open Graph 元数据，供链接卡片预览使用。
- `PlatformRelated.h` / `PlatformRelated.cpp`：平台相关辅助能力，例如粘贴注入、窗口行为和系统交互。
- `ClipboardExportService.h` / `ClipboardExportService.cpp`：剪贴板导出服务，提供 `buildMimeData()` 用于从条目构建导出载荷。
- `SingleApplication.h` / `SingleApplication.cpp`：单实例启动控制。

## 维护约定

- 修改平台相关逻辑时，同时检查 Windows / Linux 的行为是否一致。
- 新增设置项时，同时补齐默认值、读写路径和界面联动。
- 若本目录结构或职责发生变化，请同步更新本 README。

## Recent Notes
- ThemeManager now centralizes app-wide theme switching, palette updates, and QSS token resolution.
- ThemeManager now emits integer-alpha `rgba(...)` tokens for QSS instead of float-alpha forms, reducing Qt stylesheet parse failures on Windows.

- `OpenGraphFetcher` now tracks whether a fetched image is a preview or favicon to preserve link preview thumbnails.
- `ClipboardMonitor` currently prints detailed clipboard capture diagnostics (`dataChanged`, settle retries, MIME snapshot, duplicate suppression, and app-event emission) to help trace repeated copy handling.
- `PlatformRelated` now also exposes file-manager reveal helpers; on Windows it opens Explorer and selects one or more local files when the item context menu requests "Open Containing Folder".
- `MPasteSettings` 现在提供按“数值 + 单位（天 / 周 / 月）”定义的历史保留策略，供列表层按时间自动清理过期条目。
- `ClipboardMonitor` now reconnects `QClipboard::dataChanged` with `Qt::UniqueConnection`, and suppresses short-window duplicate captures with the same normalized content so one logical copy does not fan out into repeated app-level updates.
- `ClipboardMonitor` now retries short image-settle windows when the clipboard advertises image formats but the payload bytes are not ready yet.
- `ClipboardMonitor` 现在会在需要时额外等待 WPS / 金山的分阶段剪贴板写入，减少一次复制产生两条记录的问题。
- `ClipboardMonitor` 现在能在捕获阶段下载并落地 WPS 单图 HTML 载荷，方便后续统一保存为标准图片数据。
- `ClipboardMonitor` 现在会在观察到有效剪贴板变化时立刻发出早期活动信号，供界面先播提示音，再静默完成后续 settle / 抓图。
- `ClipboardMonitor` 现在会优先发出跳过立即图片解码的轻量条目，配合界面层的后台完善流程，减少复制大图时卡死主线程。
- `ClipboardMonitor` 现在会记录启动时的 prime capture 和后续捕获阶段日志，便于和界面层的 show/deferred-load 日志对照排查卡顿。
- `PlatformRelated` 现在支持可配置的自动粘贴快捷键模式，可在多种粘贴方案之间切换。

- ClipboardBoardService now owns board persistence I/O, deferred loading, and background item processing for widgets.
- ClipboardBoardService thumbnail generation now falls back to Qt serialized image payloads when PNG/JPEG bytes are unavailable.
- ClipboardBoardService now reuses ContentClassifier helpers for HTML image discovery and Qt-serialized image decoding.
- ClipboardBoardService thumbnail sizing now follows CardPreviewMetrics for consistent preview dimensions.
- Rich text thumbnails now render without extra padding to maximize preview fill.
- Rich text thumbnail generation now trims transparent margins before scaling to reduce empty borders.
- Rich text thumbnail rendering now injects wrap-safe CSS so text, tables, and inline blocks are laid out within the card width before capture.
- ClipboardBoardService now supports async thumbnail reads for visible-only UI loading.
- ClipboardBoardService now runs thumbnail generation and on-demand thumbnail reloads through a bounded worker pool instead of spawning one thread per request.
- ClipboardBoardService now treats no-progress / failed full-thumbnail rebuilds as terminal misses, so broken persisted `.mpaste` files do not trigger endless on-demand thumbnail retry loops.
- ClipboardBoardService now keeps an in-process blacklist for `.mpaste` files that already failed full reload, so visible-range thumbnail work does not keep retrying the same broken history entry.
- ClipboardBoardService now regenerates and persists visible rich-text thumbnails during on-demand thumbnail fetches, and backfills missing image thumbnails, so filtered/search results do not get stuck on loading placeholders and old single-line rich-text captures can self-heal.
- Text-only rich-text thumbnails now keep their natural layout instead of trimming-and-cover-scaling a tiny text fragment to fill the whole card.
- ClipboardBoardService now trusts `ClipboardItem::PreviewKind` when deciding whether rich text needs visual thumbnail work, reducing duplicated preview policy in the service layer.
- Text-preview rich-text items now discard persisted thumbnails during light-load/prepare paths so unused snapshots do not accumulate in memory.
- ClipboardBoardService now exposes current-category preview cache maintenance actions to repair broken previews, rebuild managed previews, or clear persisted preview thumbnails.
- ClipboardBoardService now centralizes worker-thread startup/cleanup through shared helpers instead of repeating `QThread::create` bookkeeping in each task path.
- ClipboardBoardService now supports async index scan + first-batch startup loading, so boards can show sooner before history enumeration finishes.
- Thumbnail prefetch count is now configurable via settings.

- MPasteSettings now persists a configurable save directory for external sync tools.
- `ClipboardBoardService` now exposes `containsFingerprint()` for fast duplicate lookup and `indexedItemsMeta()` for accessing the light-item index; `searchableText` in `IndexedItemMeta` is truncated to 512 characters.
- `MPasteSettings` now provides a `HistoryViewMode` enum (`ViewModePaged` / `ViewModeContinuous`) with getter, setter, and persistence support.
- New `ClipboardExportService` namespace (`ClipboardExportService.h/.cpp`) provides `buildMimeData()` for constructing clipboard export payloads from items.
