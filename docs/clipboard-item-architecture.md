# 剪贴板 Item 架构设计

## 一、核心抽象：每个 Item 的生命周期数据分层

```
┌─────────────────────────────────────────────────────────┐
│                    ClipboardItem                         │
├──────────────┬──────────────────┬────────────────────────┤
│   Level 0    │     Level 1      │       Level 2          │
│  索引元数据   │   卡片显示数据    │      完整数据           │
│  (始终在内存)  │  (可见时在内存)   │   (仅按需从磁盘读取)    │
├──────────────┼──────────────────┼────────────────────────┤
│ • name       │ • 预览缩略图      │ • 完整 MIME 数据        │
│ • fingerprint│   (QPixmap)      │   (text/html/rtf/      │
│ • contentType│ • icon (app图标)  │    image bytes 等)     │
│ • previewKind│ • previewText    │ • 完整 normalizedText  │
│ • time       │   (前500字截断)   │ • 完整 HTML            │
│ • pinned     │ • title          │ • 原始图片数据          │
│ • alias      │ • url (短字符串)  │                        │
│ • fileSize   │ • normalizedUrls │                        │
│ • textLength │   (URL列表)      │                        │
│ • hasThumbnail│• favicon        │                        │
│   Hint       │ • color          │                        │
│ • filePath   │ • imageSize      │                        │
└──────────────┴──────────────────┴────────────────────────┘
```

- Level 0 = `IndexedItemMeta`（service 层） + `loadFromFileLight(path, false)`
- Level 1 = `loadFromFileLight(path, true)` + model 中的 ClipboardItem
- Level 2 = `loadFromFile(path)` / `ensureMimeDataLoaded()`

model 中的 ClipboardItem 只持有 Level 0 + Level 1 数据。`normalizedText` 只存截断的预览文本（前 500 字符），完整文本仅在 Level 2 按需读取。

## 二、每个类型的具体行为

| 类型 | 预览缩略图生成方式 | 卡片绘制（有缩略图时） | 卡片绘制（无缩略图时） | 完整数据读取时机 |
|------|-------------------|---------------------|---------------------|----------------|
| **Text** | 不生成 | — | `drawWrappedText(previewText前500字)` | 粘贴、空格预览、详情 |
| **RichText-TextPreview** | 不生成 | — | `drawWrappedText(previewText前500字)` | 粘贴、空格预览、详情 |
| **RichText-VisualPreview** | HTML 渲染截图 | `drawManagedVisualPreview` → blit | loading 占位动画 | 粘贴、空格预览 |
| **Image** | 图片缩放裁剪 | `drawManagedVisualPreview` → blit | loading 占位动画 | 粘贴、空格预览 |
| **Office** | 同 Image | `drawManagedVisualPreview` → blit (contain) | loading 占位动画 | 粘贴 |
| **Link** | 渐变背景+badge+域名；http(s) 异步获取 og:image 覆盖 | `drawCoverPixmap` → blit + `drawLinkLabel`×2 | `linkFallbackPreview` (QCache) + `drawLinkLabel`×2 | 粘贴 |
| **File (单文件图片)** | 不生成（`localImageThumbnail` 内存 QCache） | QCache blit | 首次解码图片 | 粘贴 |
| **File (单文件非图片)** | 不生成（`localFileIcon` 系统图标 QCache） | QCache blit | — | 粘贴 |
| **File (多文件)** | 不生成 | — | 文件图标 + 文件名列表 | 粘贴 |
| **Color** | 不生成 | — | `drawRoundedRect` + 色值文本 | 粘贴 |

**需要 thumbnail 管理（`shouldManageThumbnail`）的类型**：Image、Office、Link、RichText-VisualPreview。缩略图在 `processPendingItemAsync` 后台生成、持久化到 `.mpaste` 文件，通过 `primeVisibleThumbnailsSync` / `updateVisibleThumbnails` 按可见区域加载/卸载。

**不需要 thumbnail 管理的类型**：Text、RichText-TextPreview、File、Color。卡片渲染足够轻量（截断文本、系统图标、纯色块），不值得引入 thumbnail 管理开销。

## 三、剪贴板变化的处理流程

```
剪贴板变化 (QClipboard::dataChanged)
  │
  ├─① 立即发射 clipboardActivityObserved → 播放提示音
  │
  ├─② 防抖稳定 (stabilizeTimer_ 200ms)
  │
  └─③ captureClipboard()
       │
       ├─ 从系统剪贴板快照核心数据 (text/html/urls/color/image)
       │  ※ 不读取自定义格式 (RTF/Java/Office) — 延迟到 Phase 2
       │
       ├─ createLightweight() → 构建 item (mimeDataLoaded_=false)
       │  ※ 只保留分类、预览文本、URL、图标等 Level 0+1 数据
       │
       ├─ emit clipboardUpdated(item) → MPasteWidget 处理
       │     │
       │     ├─ 重复检查 (fingerprint 比较, O(N) 遍历 model)
       │     │   ├─ 已存在 → moveItemToFirst, return
       │     │   └─ 不存在 → 继续
       │     │
       │     ├─ addOneItem() → 立即插入 model, UI 显示
       │     │
       │     ├─ saveItemQuiet() → 同步写磁盘 (不触发信号)
       │     │  ※ 必须同步: 防止 deferred loading 重载时找不到文件
       │     │
       │     └─ processPendingItemAsync() → 后台线程
       │          ├─ 生成缩略图 (Image/Office/RichText-Visual/Link)
       │          ├─ saveItemQuiet() (含缩略图)
       │          └─ emit pendingItemReady → 更新 model 中的缩略图
       │
       └─ scheduleDeferredMimeCapture() → QTimer::singleShot(0)
            ├─ 检查剪贴板未变化 (seq number)
            ├─ 读取剩余 MIME 格式 (RTF/Office 等)
            └─ emit clipboardMimeCompleted → mergeDeferredMimeFormats
                 └─ saveItemQuiet() 更新磁盘文件
```

## 四、Item 显示准备流程

```
页面加载 / 滚动触发
  │
  ├─ reloadCurrentPageItems()
  │    └─ boardService_->loadFilteredIndexedSlice()
  │         └─ loadFromFileLight(path, false)  ← 只读 Level 0 元数据
  │              ※ 不读缩略图、不读 MIME 数据
  │
  ├─ primeVisibleThumbnailsSync() — 可见区域的缩略图同步预加载
  │    └─ 对 shouldManageThumbnail() 的类型:
  │         └─ loadFromFileLight(path, true)  ← 读 Level 0 + 缩略图
  │              └─ updateItem → model 拿到缩略图
  │
  ├─ updateVisibleThumbnails() (80ms 防抖)
  │    └─ 对可见行 ± prefetch 范围:
  │         ├─ 有缩略图 → 保留
  │         ├─ 无缩略图 + hasThumbnailHint → requestThumbnailAsync (后台)
  │         └─ 离开可见区域 → 卸载缩略图 (释放内存)
  │
  └─ paint() — ClipboardCardDelegate
       ├─ 从 model 取 Level 1 数据 (icon, thumbnail, title, url, time...)
       ├─ 有 thumbnail → blit (极快)
       ├─ 无 thumbnail → 类型特定的轻量渲染 (截断文本/QCache 等)
       └─ ※ 绝不触发 Level 2 数据读取
```

## 五、完整数据（Level 2）的读取时机

仅以下操作触发 `ensureMimeDataLoaded()` / `loadFromFile()`：

| 操作 | 触发方 | 读取内容 |
|------|--------|---------|
| 双击/Enter 粘贴 | `setClipboard()` → `ClipboardExportService::buildMimeData()` | 完整 MIME |
| 空格键预览 | `ClipboardItemPreviewDialog::showItem()` | 完整 MIME + 图片 |
| 详情对话框 | `ClipboardItemDetailsDialog::showItem()` | 完整元数据 + 缩略图 |
| 右键 → 保存到文件 | `saveItemToFile()` | 完整 MIME |
| 富文本重复检测 | `addOneItem()` richerIncomingRichText 判断 | normalizedText |

## 六、关键设计原则

1. **paint() 中绝不做磁盘 I/O**。所有数据必须已在 model 中。
2. **saveItemQuiet() 不触发任何信号**。避免级联重载。
3. **QFileSystemWatcher 只响应外部变更**。通过 `hasRecentInternalWrite()` 过滤自身写入。
4. **异步索引构建时保留内存中新增的条目**。防止 `indexedItems_` 被覆写导致新 item 丢失。
5. **缩略图管理仅限需要的类型**。Text/Color/File 的渲染足够轻量，不引入管理开销。
6. **预览文本截断到 500 字符**。`previewTextForCard()` 对所有文本类型截断，即使走 fallback 渲染也快。
