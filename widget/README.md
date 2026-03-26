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
- `ClipboardBoardActionService.*`：板块条目操作 helper，承接模型/持久化层的非视觉动作。
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

## 卡片渲染逻辑（ClipboardCardDelegate）

### 各类型绘制对比

| 类型 | 有 thumbnail 时 | 无 thumbnail 时（首次/生成中） | thumbnail 生成方式 | 持久化 | 每帧数据开销 |
|------|----------------|-------------------------------|-------------------|--------|-------------|
| **Image** | `drawManagedVisualPreview` → blit | loading 占位动画 | `buildCardThumbnailImageFromBytes` — 图片缩放裁剪 | ✅ | 轻量（pixmap 引用） |
| **Office** | `drawManagedVisualPreview` → blit（contain 模式） | loading 占位动画 | 同 Image | ✅ | 轻量 |
| **RichText-VisualPreview** | `drawManagedVisualPreview` → blit | loading 占位动画 | `buildRichTextThumbnailImageFromHtml` — HTML 渲染截图 | ✅ | 轻量 |
| **RichText-TextPreview** | `drawCoverPixmap` → blit | `drawWrappedText(text.left(500))` | `buildTextPreviewImage` — 白底+前500字渲染 | ✅ | 有 thumb: int 长度；无 thumb: 500 字符串 |
| **Text** | `drawCoverPixmap` → blit | `drawWrappedText(text.left(500))` | `buildTextPreviewImage` — 同上 | ✅ | 同上 |
| **Link** | `drawCoverPixmap` → blit + `drawLinkLabel`×2（短 URL） | `linkFallbackPreview`（渐变+badge，QCache）+ `drawLinkLabel`×2 | `buildLinkPreviewImage` — 渐变背景+badge+域名 | ✅ | 有 thumb: URL 列表；无 thumb: 同+QCache |
| **Color** | — | `drawRoundedRect` + `drawElidedText(色值)` | 不生成（渲染极轻） | ❌ | 轻量 |
| **File（单文件图片）** | — | `localImageThumbnail`（QCache）→ blit | 内存 QCache，首次解码 | 内存缓存 | QCache 查找 |
| **File（单文件非图片）** | — | `localFileIcon`（系统图标，QCache） | 内存 QCache | 内存缓存 | QCache 查找 |
| **File（多文件）** | `drawCoverPixmap` → blit | 文件图标 + `joinFileNames` + 两行文本 | `buildFileListPreviewImage` — 白底+文件名列表 | ✅ | 有 thumb: URL 列表；无 thumb: 文本布局 |

### paint() 数据获取策略

| 数据字段 | 获取条件 | 开销 |
|----------|---------|------|
| `normalizedTextLength` | 总是获取 | O(1)，int |
| `normalizedText` | 仅当 `thumbnail.isNull()` 且非 Image/Office/Color | 字符串拷贝（Qt 隐式共享） |
| `normalizedUrls` | 仅当需要 text data，或类型为 Link/File | QList\<QUrl\> 拷贝 |
| 其他字段 | 总是获取 | 轻量（隐式共享/int/bool） |

### 设计原则

- 所有类型的预览都生成 thumbnail 并持久化到 `.mpaste` 文件（Color 除外，渲染本身极轻）。
- `paint()` 有 thumbnail 时只做 pixmap blit（~0.1ms），不做文本布局、渐变渲染或 URL 解析。
- thumbnail 在 `processPendingItemAsync` 后台线程生成，不阻塞 GUI。
- 下次启动时 `loadFromFileLight(path, true)` 从磁盘加载 thumbnail，无需重新生成。
- `previewTextForCard()` 对所有文本类型截断到前 500 字符，即使走 fallback 渲染也快。

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
- `ClipboardItemPreviewDialog` now reuses ContentClassifier helpers for HTML image source detection and image payload decoding.
- `ClipboardItemPreviewDialog` now supports image zoom with Ctrl+wheel and +/-/0 shortcuts in image previews.
- Clipboard card typography has been reduced slightly to make list items feel lighter.
- `ClipboardCardDelegate` 现在会缓存头部主色、头部图标和链接占位预览，并把单文件卡片的本地图预览读取移到后台线程，避免在 `paint` 里同步解码磁盘图片。
- `ClipboardCardDelegate` 现在把单文件卡片的系统文件图标提取移出 `paint`，改为事件循环中按队列回填，减少滚动时的同步图标提取卡顿。
- `ClipboardCardDelegate` 现在会给卡片内生成的标题、正文、页脚和占位预览文本显式指定 UI 字体族，减少 Windows 下落到 `Fixedsys`/DirectWrite fallback 时的首帧抖动。
- `ClipboardCardDelegate` 现在把运行时 pixmap 缓存预算收紧到更保守的范围，并移除了单文件图标的一层重复缓存，避免滚动优化把常驻内存顶到数百 MB。
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
- Hover action bar now uses a translucent painted background instead of a native acrylic child window, avoiding geometry overflow warnings and reducing scroll-time UI-thread interruptions on Windows.
- Hover action bar now anchors to the stable board host instead of the scrolling viewport, avoiding coordinate drift on very long horizontal histories.
- Card headers now use YaHei UI with a larger gap between type and time, and a slightly larger type label.
- Clipboard board model now refreshes rows when link preview metadata (thumbnail/favicon/title) changes.
- Arrow-key navigation now keeps the selected card at the edge until it needs to scroll.
- Favorites are now treated as a separate preserved board: deleting an item from Clipboard no longer affects Favorites, deleting from Favorites behaves like un-starring, and time-based history cleanup skips the Favorites board entirely.
- `MPasteWidget` currently prints clipboard-update and sound-play diagnostics so repeated copy prompts can be traced from app-level updates to actual sound playback decisions.
- `MPasteWidget` 和 `ScrollItemsWidget` 现在会输出启动、窗口 show、延迟历史加载和后台条目补全的阶段耗时日志，方便定位卡顿发生在哪一步。
- `MPasteWidget` 现在会在监控器首次观察到有效复制动作时立即播放提示音，不再等待后续 WPS settle / 抓图完成。
- `ScrollItemsWidget` 会把纯文本粘贴请求继续转发给主窗口。
- `ScrollItemsWidget` 现在支持“分页 / 连续列表”两种历史浏览模式：默认按固定 50 条做页窗口显示，只加载当前页所需的历史条目；切到连续列表模式后恢复按滚动延迟补载，主窗口只在分页模式下显示手动页码选择。
- `ScrollItemsWidget` now uses the service-layer light-item index cache for page reloads and filter switches, so pagination does not need to re-read every history file just to rebuild the visible slice.
- In paged mode, type/keyword switches now slice directly from the service-layer light-item index instead of repopulating the full board model and letting proxy filters trim it afterward, reducing rich-text category switch stalls.
- Managed visual-preview cards (image / office / rich-text snapshot) now expose a unified preview state through the board model, so the delegate renders one consistent ready/loading/unavailable state machine instead of inferring behavior separately per card type.
- Paged mode now keeps delegate-side visual caches on a much tighter budget and clears them on page/filter boundary changes, so off-page image previews do not linger as aggressively in memory.
- Switching to non-visual filters (for example plain text) now also strips visual thumbnails out of the current board model, so hidden image/rich-text previews stop retaining pixmaps even before the next page rebuild.
- `ScrollItemsWidget` no longer performs automatic on-disk deletion while loading, filtering, replacing richer duplicates, or applying retention cleanup; only explicit user delete actions should remove persisted `.mpaste` files.
- `ScrollItemsWidget` 现在会在主窗口卡片右键菜单中开放保存功能：图片导出为图片文件、富文本导出为 HTML、纯文本导出为 TXT，其他类型暂不开放保存。
- `ScrollItemsWidget` 现在支持 `Ctrl/Shift` 多选，并在右键菜单中提供批量收藏、批量取消收藏和批量删除；多选时会隐藏单卡片悬浮工具条，主窗口计数区会显示“已选/总数”。
- `ScrollItemsWidget` 现在会在搜索/类型筛选切换后重新触发可见条目的缩略图回补，避免富文本/图片结果一直停留在 loading 占位。
- `ScrollItemsWidget` 现在在窗口隐藏阶段只做 light-item 预热，不触发缩略图调度、首选中和视口刷新；这些 UI 相关工作会在首次真正显示时再补上。
- `ClipboardBoardView` 现在会在滚动/重绘期间每秒输出一次 `board-fps` 日志，包含 `fps`、`maxFrameGapMs`、`maxPaintMs` 和长帧计数，方便直接观察列表是否接近 60Hz。
- `ScrollItemsWidget` 现在按“进入/离开可见区”的差量方式管理缩略图，不再在每次滚动后按总条目数全量扫描整个模型。
- `ScrollItemsWidget` 的 loading shimmer 现在只重绘当前可见且仍缺缩略图的卡片区域，减少滚动过程中的整视口重绘。
- `ClipboardBoardModel` 现在维护 `name -> row` 索引，并在缩略图/元数据变更时只发相关 roles，降低回补完成后的查找和重绘成本。
- 富文本卡片现在统一回到截图缩略图预览，避免 `QTextBrowser` / 文字预览在卡片区域内出现排版和观感问题。
- 富文本卡片继续消费数据层给出的 `PreviewKind`，但当前策略固定为视觉缩略图，界面层不再走文字预览分支；新复制条目若首次缩略图未及时生成，会自动补做异步快照而不是长期停留在 loading。
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
- `MPasteSettingsWidget` 现在将设置页重构为“常用设置 / 快捷与预览 / 同步与维护”三页签布局，提升操作按钮高度，并改为固定宽度 + 按内容自适应高度，避免底部空白和按钮裁切。
- `MPasteWidget` 现在会在设置窗口打开期间临时停用全局唤起热键，避免编辑快捷键时被当前热键立即抢走。
- `ScrollItemsWidget` 现在会在列表左右预留呼吸边距，并在视口边缘绘制接近主窗口淡灰玻璃底色的轻雾渐变遮罩，让横向滚动时更接近贴边淡出的效果。
- 文件类卡片现在也会像链接卡片一样隐藏底部 `infoWidget`，让缩略图区域更完整。
- `ScrollItemsWidget` 现在会在新增图片条目时按 `CardPreviewMetrics` 提供的逻辑尺寸和较高 device pixel ratio 生成横向居中缩略图，减少首个图片条目发糊。
- 链接预览图和图片卡片现在都优先占满可用高度，只在宽度超出时做左右居中裁剪。
- 卡片底部 info 条高度已压缩，减少预览被占用的垂直空间。
- 富文本/文本卡片预览现在保留少量内边距，避免内容贴边过紧。
- 列表现在按可见范围按需加载缩略图，离屏条目会释放缩略图以降低内存占用。
- 列表加载中会显示居中 loading 提示，加载完成后自动隐藏。
- 缩略图在滚动过程中按可见范围附近约 50 个条目动态加载与释放，避免全量常驻。
- 链接预览缩略图现在也会像图片/富文本一样在离屏后释放，并在重新进入可见范围时按需从磁盘恢复，减少滚动到末尾后的常驻内存。
- 图片/富文本在缩略图未就绪时显示加载占位，避免文字与图片闪烁切换。
- 设置里新增了预览缓存数量配置，可调整缩略图预取数量。

- ScrollItemsWidget now delegates persistence, deferred loading, and background item completion to ClipboardBoardService.
- ScrollItemsWidget now routes item metadata persistence, export, favorite toggles, and delete mutations through ClipboardBoardActionService instead of directly mutating model/persistence code everywhere.

- Card headers now show custom aliases above the type/time line when provided.

- ClipboardItemRenameDialog provides the themed rename prompt used by card context menus.
- ClipboardItemRenameDialog now activates the window and focuses the input on show.

- Settings now expose the sync folder path for external sync tools.
- Settings now expose current-category preview cache maintenance actions for repairing broken previews, rebuilding preview cache, and clearing persisted previews.

- The main window now watches the save folder for external sync changes and reloads history with a debounce.

- Cards can be pinned to the top via the hover bar or context menu.

- `ClipboardBoardProxyModel` now supports pagination with `setPageSize`/`setPageIndex`/`pageSize`/`pageIndex` methods.
- `ClipboardCardDelegate` cache overhaul: `cardPixmapCache` now uses a count-based `QCache` (60 entries, cost=1 each) instead of byte-size budgeting; cache key simplified to just item name, with selection border and shortcut text drawn as overlays.
- `ClipboardCardDelegate` now provides `preRenderAll()` to eagerly cache all visible cards, then release intermediate data (thumbnails, icons, favicons); separate `clearIntermediateCaches()` preserves `cardPixmapCache` across page reloads.
- `ClipboardCardDelegate` now provides `invalidateCard()` for targeted cache invalidation (e.g., after og:image fetch) and `isCardCached()` / `drawSelectionBorder()` / `drawShortcutOverlay()` query/draw helpers.
- Link cards now use a unified browser-window fallback style, favicon badge enlarged (0.56 scale), og:image checked via `isLikelyLinkPreviewImage`; link card footer now shows the URL (was hidden before).
- Icon shadow now rendered as a black silhouette with alpha mask; header type text uses Microsoft YaHei font; app icon enlarged to 40px (downscaled to max 64px on load).
- `ScrollItemsWidgetMV`: after `preRenderAll`, releases thumbnail/icon/favicon from model items and clears intermediate caches; pagination enforces `PAGE_SIZE` when new items arrive.
- `ScrollItemsWidgetMV`: duplicate detection now checks `boardService` index with content-based fallback when the model is not fully loaded.
- `ScrollItemsWidgetMV`: multi-select delete fix falls back to `selectedIndexes()` when `selectedRows()` returns empty.
- `ScrollItemsWidgetMV`: scroll performance improved by using `loadingPhase` only in cache key for loading cards and avoiding unnecessary page reloads.
- `MPasteSettingsWidget` now emits `historyViewModeChanged` signal.
- `MPasteWidget` now defers `primeCurrentClipboard` until board loading completes.
