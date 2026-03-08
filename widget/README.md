<!--
input: 依赖所属目录的真实结构、职责与文件变化。
output: 对外提供目录级架构说明与文件清单。
pos: 目录级维护文档与变更约束入口。
update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
-->

# widget

> 一旦我所属的文件夹有所变化，请更新我。
- 定位：widget 层承载窗口、卡片、设置与预览等界面组件。
- 依赖：Qt Widgets 以及 data/utils 层能力。
- 输出：可交互的剪贴板管理界面。

## Files
- `AboutWidget.cpp`: 地位=实现文件；功能=实现 AboutWidget 的运行逻辑与行为。
- `AboutWidget.h`: 地位=接口声明；功能=声明 AboutWidget 的公开类型、信号、槽或函数。
- `AboutWidget.ui`: 地位=界面描述；功能=定义 AboutWidget 的 Qt Designer 布局。
- `ClipboardItemInnerWidget.cpp`: 地位=实现文件；功能=实现 ClipboardItemInnerWidget 的运行逻辑与行为。
- `ClipboardItemInnerWidget.h`: 地位=接口声明；功能=声明 ClipboardItemInnerWidget 的公开类型、信号、槽或函数。
- `ClipboardItemInnerWidget.ui`: 地位=界面描述；功能=定义 ClipboardItemInnerWidget 的 Qt Designer 布局。
- `ClipboardItemWidget.cpp`: 地位=实现文件；功能=实现 ClipboardItemWidget 的运行逻辑与行为。
- `ClipboardItemWidget.h`: 地位=接口声明；功能=声明 ClipboardItemWidget 的公开类型、信号、槽或函数。
- `FileThumbWidget.cpp`: 地位=实现文件；功能=实现 FileThumbWidget 的运行逻辑与行为。
- `FileThumbWidget.h`: 地位=接口声明；功能=声明 FileThumbWidget 的公开类型、信号、槽或函数。
- `FileThumbWidget.ui`: 地位=界面描述；功能=定义 FileThumbWidget 的 Qt Designer 布局。
- `MPasteSettingsWidget.cpp`: 地位=实现文件；功能=实现 MPasteSettingsWidget 的运行逻辑与行为。
- `MPasteSettingsWidget.h`: 地位=接口声明；功能=声明 MPasteSettingsWidget 的公开类型、信号、槽或函数。
- `MPasteSettingsWidget.ui`: 地位=界面描述；功能=定义 MPasteSettingsWidget 的 Qt Designer 布局。
- `MPasteWidget.cpp`: 地位=实现文件；功能=实现 MPasteWidget 的运行逻辑与行为。
- `MPasteWidget.h`: 地位=接口声明；功能=声明 MPasteWidget 的公开类型、信号、槽或函数。
- `MPasteWidget.ui`: 地位=界面描述；功能=定义 MPasteWidget 的 Qt Designer 布局。
- `MTextBrowser.cpp`: 地位=实现文件；功能=实现 MTextBrowser 的运行逻辑与行为。
- `MTextBrowser.h`: 地位=接口声明；功能=声明 MTextBrowser 的公开类型、信号、槽或函数。
- `README.md`: 地位=目录说明；功能=总结本目录职责、约束与文件清单。
- `ScrollItemsWidget.cpp`: 地位=实现文件；功能=实现 ScrollItemsWidget 的运行逻辑与行为。
- `ScrollItemsWidget.h`: 地位=接口声明；功能=声明 ScrollItemsWidget 的公开类型、信号、槽或函数。
- `ScrollItemsWidget.ui`: 地位=界面描述；功能=定义 ScrollItemsWidget 的 Qt Designer 布局。
- `ToggleSwitch.cpp`: 地位=实现文件；功能=实现 ToggleSwitch 的运行逻辑与行为。
- `ToggleSwitch.h`: 地位=接口声明；功能=声明 ToggleSwitch 的公开类型、信号、槽或函数。
- `WebLinkThumbWidget.cpp`: 地位=实现文件；功能=实现 WebLinkThumbWidget 的运行逻辑与行为。
- `WebLinkThumbWidget.h`: 地位=接口声明；功能=声明 WebLinkThumbWidget 的公开类型、信号、槽或函数。
- `WebLinkThumbWidget.ui`: 地位=界面描述；功能=定义 WebLinkThumbWidget 的 Qt Designer 布局。

## Recent Notes
- `ClipboardItemWidget` now exposes a context-menu action for plain-text paste.
- `MPasteWidget` now supports `Ctrl+Enter` and `Alt+Shift+[1-9,0]` to paste as plain text.
- `ScrollItemsWidget` forwards plain-text paste requests from item cards to the main window.
