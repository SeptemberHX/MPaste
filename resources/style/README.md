<!--
input: 依赖所属目录的真实结构、职责与文件变化。
output: 对外提供目录级架构说明与文件清单。
pos: 目录级维护文档与变更约束入口。
update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
-->

# resources/style

> 一旦我所属的文件夹有所变化，请更新我。
- 定位：resources/style 目录承载 QSS 主题样式。
- 依赖：Qt 样式系统与资源路径约定。
- 输出：全局外观与控件视觉规则。

## Files
- `defaultStyle.qss`: 地位=样式表；功能=定义 defaultStyle 的外观规则。

## Recent Notes
- `defaultStyle.qss` now renders `countArea` as a smaller, lighter translucent capsule badge so it does not fight with the frosted window background.
- `README.md`: 地位=目录说明；功能=总结本目录职责、约束与文件清单。
