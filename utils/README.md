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
- `SingleApplication.h` / `SingleApplication.cpp`：单实例启动控制。

## 维护约定

- 修改平台相关逻辑时，同时检查 Windows / Linux 的行为是否一致。
- 新增设置项时，同时补齐默认值、读写路径和界面联动。
- 若本目录结构或职责发生变化，请同步更新本 README。

## Recent Notes
- ThemeManager now centralizes app-wide theme switching, palette updates, and QSS token resolution.

- `OpenGraphFetcher` now tracks whether a fetched image is a preview or favicon to preserve link preview thumbnails.
- `ClipboardMonitor` currently prints detailed clipboard capture diagnostics (`dataChanged`, settle retries, MIME snapshot, duplicate suppression, and app-event emission) to help trace repeated copy handling.
- `PlatformRelated` now also exposes file-manager reveal helpers; on Windows it opens Explorer and selects one or more local files when the item context menu requests "Open Containing Folder".
- `MPasteSettings` 现在提供按“数值 + 单位（天 / 周 / 月）”定义的历史保留策略，供列表层按时间自动清理过期条目。
- `ClipboardMonitor` now reconnects `QClipboard::dataChanged` with `Qt::UniqueConnection`, and suppresses short-window duplicate captures with the same normalized content so one logical copy does not fan out into repeated app-level updates.
- `ClipboardMonitor` 现在会在需要时额外等待 WPS / 金山的分阶段剪贴板写入，减少一次复制产生两条记录的问题。
- `ClipboardMonitor` 现在能在捕获阶段下载并落地 WPS 单图 HTML 载荷，方便后续统一保存为标准图片数据。
- `ClipboardMonitor` 现在会在观察到有效剪贴板变化时立刻发出早期活动信号，供界面先播提示音，再静默完成后续 settle / 抓图。
- `ClipboardMonitor` 现在会优先发出跳过立即图片解码的轻量条目，配合界面层的后台完善流程，减少复制大图时卡死主线程。
- `ClipboardMonitor` 现在会记录启动时的 prime capture 和后续捕获阶段日志，便于和界面层的 show/deferred-load 日志对照排查卡顿。
- `PlatformRelated` 现在支持可配置的自动粘贴快捷键模式，可在多种粘贴方案之间切换。

- ClipboardBoardService now owns board persistence I/O, deferred loading, and background item processing for widgets.
