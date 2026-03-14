<!--
input: 依赖 resources/style 目录的实际结构与样式职责。
output: 提供 resources/style 目录的中文说明与文件清单。
pos: 样式目录说明文档。
update: 修改本目录文件时，同步更新本 README。
-->

# resources/style

> `resources/style` 目录存放应用的 QSS 样式文件，用于定义主窗口与常用控件的视觉外观。

## 目录职责

- 统一维护窗口、按钮、输入框、滚动区域等控件的默认样式。
- 为应用的玻璃质感、轻量边框和菜单视觉提供集中配置入口。

## 文件说明

- `defaultStyle.qss`：应用默认样式表。
- `darkStyle.qss`???????????
- `README.md`：当前目录说明文档。

## 维护约定

- 修改样式时，优先保持主窗口、菜单、卡片与设置界面的视觉一致性。
- 若样式新增了强依赖资源，请同步确认资源路径和 `resources.qrc` 配置。
- 若本目录结构或职责发生变化，请同步更新本 README。

## Recent Notes

- ?? `darkStyle.qss`???????????????????

- `defaultStyle.qss` 现在把 `countArea` 调整为更轻的半透明胶囊徽标。
- 默认样式现在为主窗口溢出菜单按钮与 `QMenu` 提供了更统一的玻璃化外观。
