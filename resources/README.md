<!--
input: 依赖 resources 目录的实际资源结构与用途说明。
output: 提供 resources 目录的中文说明与文件清单。
pos: 资源目录说明文档。
update: 修改本目录文件时，同步更新本 README。
-->

# resources

> `resources` 目录存放应用打包进程序的静态资源，包括图标、音效、翻译产物和样式子目录。

## 目录职责

- 提供界面图标、菜单图标和状态图标等 SVG / PNG 资源。
- 提供应用提示音等多媒体资源。
- 承载编译后的翻译资源与样式目录入口。

## 文件说明

- `*.svg`：界面图标资源，例如保存、删除、详情、设置、收藏、菜单等按钮图标。
- `mpaste.png` / `mpaste.svg`：应用图标资源。
- `sound.mp3`：复制提示音。
- `app_zh.qm`：中文翻译编译产物。
- `style/`：QSS 样式目录。
- `README.md`：当前目录说明文档。

## 维护约定

- 新增资源时，确认已经加入 `resources.qrc` 并且命名语义清晰。
- 替换图标时，注意界面中现有尺寸、颜色和可读性是否仍然合适。
- 若本目录结构或职责发生变化，请同步更新本 README。

## Recent Notes

- 新增了深色主题下使用的 `*_light.svg` 亮色图标变体，用于提升图标对比度。
- `resources/style/darkStyle.qss` 现在承载深色主题的额外样式覆盖。
- 新增了 `menu_more.svg`、`settings.svg`、`info.svg` 和 `quit.svg`，用于主窗口溢出菜单与图标按钮。
- Added rename icon variants (`rename.svg`, `rename_light.svg`) for context menu actions.
- Added `page_chevron.svg` and `page_chevron_light.svg` for pagination controls.
