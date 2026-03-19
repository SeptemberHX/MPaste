<!--
input: 依赖 widget 目录的实际结构、界面职责与近期实现变化。
output: 提供 widget 目录的中文说明与文件清单。
pos: 界面层目录说明文档。
update: 修改本目录文件时，同步更新本 README。
-->

# widget

> `widget` 目录负责 MPaste 的界面层实现，包括主窗口、设置页、详情弹窗以及剪贴板卡片的展示与交互。

## 目录职责

- 提供主窗口、关于页、设置页和条目详情等界面组件。
- 负责剪贴板卡片、列表滚动、缩略图与链接预览等视觉呈现。
- 处理界面层交互，如悬浮操作、筛选、滚动、快捷选择和上下文菜单。

## 组件说明

- `AboutWidget.*`：关于窗口。
- `ClipboardItemDetailsDialog.*`：条目详情与检查器弹窗。
- `MPasteSettingsWidget.*`：设置窗口。
- `MPasteWidget.*`：主窗口与主要交互入口。
- `MTextBrowser.*`：用于卡片正文展示的轻量文本浏览控件。
- `ScrollItemsWidget.*`：横向滚动的条目列表容器。
- `ToggleSwitch.*`：设置界面中使用的开关控件。

## 维护约定

- 修改 `.ui` 文件时，保持对应的 `.h/.cpp` 行为一致。
- 修改卡片尺寸、滚动策略或窗口结构时，同时检查主窗口布局与交互体验。
- 若本目录结构或职责发生变化，请同步更新本 README。

## Recent Notes
- UI components now listen to ThemeManager for theme updates instead of coordinating via the main window.
- System tray context menus now receive the themed dark palette and stylesheet to keep text/icon contrast consistent.
- Auto-paste now attempts a disk rehydrate fallback when clipboard MIME data is missing, keeping favorites usable.
- Favoriting now tries to rehydrate full MIME data before persisting, reducing light-item paste failures.
- Renames/pin toggles now reload persisted offsets to keep post-edit pastes working.
- Alias edits now sync across boards so favorites and clipboard stay consistent.
- Metadata-only saves now rehydrate full items to avoid MIME loss after edits.
- Clipboard paste now falls back to plain text when MIME payloads are missing or empty.
- Alias/pin edits now update only metadata on disk, leaving MIME blobs untouched.
- Type filters now include Office shapes for editable PPT/Word objects.
- ???????????/??/?????????????????????

- `ClipboardItemPreviewDialog` now provides a larger centered preview window for rich text, plain text, images, and files; it is read-only but still allows selection and copy, can be opened from the item preview menu action or the Space key on the current supported selection, and pressing Space again closes it.
- `ClipboardItemPreviewDialog` now uses a tuned body font size and hides the text caret by disabling focus, while keeping mouse selection/copy behavior.
- `ClipboardItemPreviewDialog` now decodes Qt serialized image payloads when standard image bytes are unavailable.
- Clipboard card typography has been reduced slightly to make list items feel lighter.
- File-type cards now show image thumbnails when they contain a single local image file.
- File-type cards now show the single file path in the footer info line.
- Single-file paths in the footer now use middle elide to keep filenames visible.
- Footer spacing now adapts to shortcut text width to keep the path closer.
- Link card URL rows now reserve space for shortcut text.
- Multi-file cards now use a file-icon + two-line filename summary preview aligned with the legacy file thumbnail style.
- Single-file cards now keep the file icon centered even when the system icon renders smaller than the slot.
- Link preview fallback images no longer render the small caption line inside the generated preview block.
- Link previews now use OpenGraph/first-image thumbnails when available; favicon-only fallbacks render a larger badge.
- Link cards now trigger OpenGraph fetches on selection to populate missing thumbnails in delegate mode.
- New link items now start OpenGraph fetches immediately so thumbnails arrive without needing extra navigation.
- Delegate mode now uses a single reusable hover action bar to show card tools without per-item widgets.
- Hover action bar now uses a frosted acrylic-style background (native blur on Windows, translucent fallback elsewhere).
- Card headers now use YaHei UI with a larger gap between type and time, and a slightly larger type label.
- Clipboard board model now refreshes rows when link preview metadata (thumbnail/favicon/title) changes.
- Arrow-key navigation now keeps the selected card at the edge until it needs to scroll.
- Favorites are now treated as a separate preserved board: deleting an item from Clipboard no longer affects Favorites, deleting from Favorites behaves like un-starring, and time-based history cleanup skips the Favorites board entirely.
- `MPasteWidget` currently prints clipboard-update and sound-play diagnostics so repeated copy prompts can be traced from app-level updates to actual sound playback decisions.
- `MPasteWidget` 和 `ScrollItemsWidget` 现在会输出启动、窗口 show、延迟历史加载和后台条目补全的阶段耗时日志，方便定位卡顿发生在哪一步。
- `MPasteWidget` 现在会在监控器首次观察到有效复制动作时立即播放提示音，不再等待后续 WPS settle / 抓图完成。
- `ScrollItemsWidget` 会把纯文本粘贴请求继续转发给主窗口。
- `ScrollItemsWidget` 现在会先快速插入轻量条目，再在后台线程补做缩略图和保存落盘，减少复制大图时卡住界面。
- `ClipboardItemDetailsDialog` 现在提供更完整的条目检查视图，可查看归一化结果与原始 MIME 数据。
- `ClipboardItemDetailsDialog` 现在锁定了检查器宽度，并让概览页长链接/长标题优先在窗口内换行，避免详情窗口被内容横向撑开。
- `ClipboardItemDetailsDialog` 现在移除了外层卡片阴影，让详情窗口里的组件视觉更干净。
- `ClipboardItemDetailsDialog` 现在整体调大了标题、字段值、标签页和编辑器字号，提升可读性。
- `ClipboardItemDetailsDialog` 现在会显示当前条目在当前列表里的序号，方便确认它是第几个条目。
- `ClipboardItemDetailsDialog` 现在会在主预览图下方额外显示横向缩略图，并在摘要里标出缩略图尺寸。
- `ClipboardItemDetailsDialog` 现在会按标签的 device pixel ratio 先缩放主预览和缩略图，减少高分屏下的发糊。
- `ClipboardItemDetailsDialog` 现在会对带缩略图的富文本条目直接显示其快照缩略图，而不是退回默认图标。
- 图片与富文本缩略图现在统一按目标卡片框做 `KeepAspectRatioByExpanding` 的居中 cover 裁切，避免长条图生成过窄的缩略图。
- `MPasteWidget` 现在会在短时间内抑制重复提示音，减少一次复制触发多次响声的问题。
- `MPasteWidget` 现在会在每次播放提示音前按当前默认输出设备重建播放链路，减少运行中切换耳机后提示音仍走旧设备的问题，同时避免设备变化回调触发的重复重建。
- `MPasteWidget` 现在会把提示音播放器指针初始化为 `nullptr`，避免重建播放链路时因未初始化指针导致的启动崩溃。
- `MPasteSettingsWidget` 现在会在快捷键输入框里主动记录 `Win`/`Meta` 组合键，避免 `QKeySequenceEdit` 在 Windows 下录不进 `Win+...`。
- `MPasteWidget` 现在会在设置窗口打开期间临时停用全局唤起热键，避免编辑快捷键时被当前热键立即抢走。
- `ScrollItemsWidget` 现在会在列表左右预留呼吸边距，并在视口边缘绘制接近主窗口淡灰玻璃底色的轻雾渐变遮罩，让横向滚动时更接近贴边淡出的效果。
- 文件类卡片现在也会像链接卡片一样隐藏底部 `infoWidget`，让缩略图区域更完整。
- `ScrollItemsWidget` 现在会在新增图片条目时按 `275x218` 逻辑尺寸和较高 device pixel ratio 生成横向居中缩略图，减少首个图片条目发糊。
- 链接预览图和图片卡片现在都优先占满可用高度，只在宽度超出时做左右居中裁剪。

- ScrollItemsWidget now delegates persistence, deferred loading, and background item completion to ClipboardBoardService.

- Card headers now show custom aliases above the type/time line when provided.

- ClipboardItemRenameDialog provides the themed rename prompt used by card context menus.

- Settings now expose the sync folder path for external sync tools.

- The main window now watches the save folder for external sync changes and reloads history with a debounce.

- Cards can be pinned to the top via the hover bar or context menu.
