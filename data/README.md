<!--
input: 依赖 data 目录的实际结构、职责分工与近期实现变化。
output: 提供 data 目录的中文说明与文件清单。
pos: 数据层目录说明文档。
update: 修改本目录文件时，同步更新本 README。
-->

# data

> `data` 目录负责剪贴板条目的数据模型、持久化存储以及链接元数据结构。

## 目录职责

- 统一定义剪贴板条目的数据表示、比较逻辑与类型识别结果。
- 负责当前 `.mpaste` 文件的读写与目录级加载。
- 为链接预览等功能提供可持久化的结构体模型。

## 文件说明

- `ClipboardItem.h`：剪贴板条目核心模型，负责文本、图片、文件、链接等内容的统一表示。
- `CardPreviewMetrics.h`：卡片预览尺寸常量，供缩略图生成与 UI 渲染保持一致。
- `LocalSaver.h` / `LocalSaver.cpp`：本地存储服务，负责当前格式条目的序列化、反序列化与懒加载。
- `OpenGraphItem.h` / `OpenGraphItem.cpp`：链接元数据模型，用于保存标题、描述、预览图等信息。

## 维护约定

- 新增数据字段时，同时检查当前序列化格式与轻加载路径是否一致。
- 修改条目判等、指纹或归一化逻辑时，注意去重、筛选和保存路径是否受到影响。
- 若本目录结构或职责发生变化，请同步更新本 README。

## Recent Notes

- `PreviewKind.h` / `PreviewClassifier.h`: shared preview mode enum + classifier used by cards and thumbnail workers.
- `OpenGraphItem` now tracks whether the fetched image is a preview or favicon, so link cards can render the correct thumbnail style.
- `LocalSaver` now preserves alias/pin metadata when rehydrating MIME payloads.
- `LocalSaver` now falls back to cached text/URLs when MIME payloads are missing or empty to avoid empty pastes.
- `LocalSaver` now supports metadata-only updates that preserve existing MIME blobs.
- Clipboard items now preserve Windows clipboard MIME formats in lightweight capture to keep Office shapes editable.
- Clipboard items now classify Office clipboard payloads as a distinct content type.
- Content classification is centralized in `ContentClassifier` with shared `ContentType`.
- ContentClassifier now exposes shared image format lists and HTML/image helpers to keep thumbnail + preview decoding consistent.
- Clipboard items now prefer decodable image MIME bytes in lightweight capture and classify light items using the source clipboard snapshot to avoid blank image cards.
- `ClipboardItem` 现在会缓存可搜索纯文本，减少重复关键字扫描开销。
- `ClipboardItem` 现在提供轻量级内容指纹，用于更快地定位去重候选项。
- `ClipboardItem` 现在会把协议文本和原始 MIME 数据统一归一化为 URLs / 文本，供共享的类型识别逻辑使用。
- `ClipboardItem` 现在支持创建保留原始 MIME 但跳过立即图片物化的轻量快照条目，减少复制大图时的主线程阻塞。
- `LocalSaver` 现在只读写当前 `.mpaste v4`，并依赖 `CardPreviewMetrics` 提供的预览逻辑尺寸与 HiDPI backing pixels 的居中缩略图及 MIME 偏移来支持轻加载。
- `LocalSaver` 现在只会在轻加载阶段把“无文本、无链接但带缩略图且头部漂移成 Text”的条目纠正回 `Image`，避免富文本快照被误判成图片。
- `ClipboardItem` 现在会优先把已物化的本地图片 MIME 载荷判成 `Image`，避免带着 WPS HTML 的条目在后续显示时又回退到远程 HTML 图片路径。

- `ClipboardItem` now exposes a data-layer `PreviewKind` so rich-text cards and thumbnail workers can share one text-vs-visual preview decision instead of re-deriving it independently.
- `LocalSaver` now exposes a thumbnail-only header rewrite path so preview cache maintenance can clear/rebuild persisted thumbnails without rewriting MIME payload blobs.
- LocalSaver now writes .mpaste v5 with custom alias metadata while still reading v4.

- LocalSaver now writes .mpaste v6 with alias + pin metadata while still reading v4/v5.
