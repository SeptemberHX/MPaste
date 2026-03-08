<!--
input: 依赖所属目录的真实结构、职责与文件变化。
output: 对外提供目录级架构说明与文件清单。
pos: 目录级维护文档与变更约束入口。
update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
-->

# data

> 一旦我所属的文件夹有所变化，请更新我。
- 定位：data 层承载剪贴板数据模型、序列化与持久化规则。
- 依赖：Qt Core/Gui 的 mime、图像与时间类型。
- 输出：可存储、可比较、可恢复的数据对象。

## Files
- `ClipboardItem.h`: 地位=接口声明；功能=声明 ClipboardItem 的公开类型、信号、槽或函数。
- `LocalSaver.cpp`: 地位=实现文件；功能=实现 LocalSaver 的运行逻辑与行为。
- `LocalSaver.h`: 地位=接口声明；功能=声明 LocalSaver 的公开类型、信号、槽或函数。
- `OpenGraphItem.cpp`: 地位=实现文件；功能=实现 OpenGraphItem 的运行逻辑与行为。
- `OpenGraphItem.h`: 地位=接口声明；功能=声明 OpenGraphItem 的公开类型、信号、槽或函数。
- `README.md`: 地位=目录说明；功能=总结本目录职责、约束与文件清单。

## Recent Notes
- `ClipboardItem` now caches searchable plain-text content to reduce repeated keyword scan cost.
- `ClipboardItem` now exposes a lightweight content fingerprint for dedup candidate lookup.
- `ClipboardItem` now avoids deprecated Qt hash APIs to keep Qt 6 builds warning-clean.
- `ClipboardItem` now standardizes protocol text and raw MIME payloads into normalized URLs/text for shared type recognition.
- Normalization now only trusts explicit URL/file evidence (`urls`, `text/uri-list`, `x-special/gnome-copied-files`, `x-special/nautilus-clipboard`) and no longer upgrades ordinary plain text heuristically.
- Native URL MIME is now only trusted when it is local-file data or matches the visible text payload, avoiding accidental promotion from stray platform clipboard formats.
- Explicit private formats such as Windows `FileName(W)` and `UniformResourceLocator(W)` are now normalized alongside Linux file-copy protocols.
- `LocalSaver` now writes `.mpaste v3` and migrates legacy/v2 files to v3 automatically when a save directory is loaded.
