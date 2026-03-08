<!--
input: 依赖所属目录的真实结构、职责与文件变化。
output: 对外提供目录级架构说明与文件清单。
pos: 目录级维护文档与变更约束入口。
update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
-->

# utils

> 一旦我所属的文件夹有所变化，请更新我。
- 定位：utils 层承载跨平台系统能力与通用服务。
- 依赖：Qt 平台抽象、系统 API 与网络能力。
- 输出：监控、热键、单实例、平台桥接等基础服务。

## Files
- `ClipboardMonitor.cpp`: 地位=实现文件；功能=实现 ClipboardMonitor 的运行逻辑与行为。
- `ClipboardMonitor.h`: 地位=接口声明；功能=声明 ClipboardMonitor 的公开类型、信号、槽或函数。
- `HotKeyManager.cpp`: 地位=实现文件；功能=实现 HotKeyManager 的运行逻辑与行为。
- `HotKeyManager.h`: 地位=接口声明；功能=声明 HotKeyManager 的公开类型、信号、槽或函数。
- `MPasteSettings.cpp`: 地位=实现文件；功能=实现 MPasteSettings 的运行逻辑与行为。
- `MPasteSettings.h`: 地位=接口声明；功能=声明 MPasteSettings 的公开类型、信号、槽或函数。
- `OpenGraphFetcher.cpp`: 地位=实现文件；功能=实现 OpenGraphFetcher 的运行逻辑与行为。
- `OpenGraphFetcher.h`: 地位=接口声明；功能=声明 OpenGraphFetcher 的公开类型、信号、槽或函数。
- `PlatformRelated.cpp`: 地位=实现文件；功能=实现 PlatformRelated 的运行逻辑与行为。
- `PlatformRelated.h`: 地位=接口声明；功能=声明 PlatformRelated 的公开类型、信号、槽或函数。
- `README.md`: 地位=目录说明；功能=总结本目录职责、约束与文件清单。
- `SingleApplication.cpp`: 地位=实现文件；功能=实现 SingleApplication 的运行逻辑与行为。
- `SingleApplication.h`: 地位=接口声明；功能=声明 SingleApplication 的公开类型、信号、槽或函数。
