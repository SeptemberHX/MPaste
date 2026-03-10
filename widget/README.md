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
- `ClipboardItemDetailsDialog.*`：条目详情与检查器弹窗。
- `ClipboardItemInnerWidget.*`：条目卡片内部主体内容渲染。
- `ClipboardItemWidget.*`：条目卡片外层包装与卡片级交互。
- `FileThumbWidget.*`：文件条目的缩略图展示。
- `MPasteSettingsWidget.*`：设置窗口。
- `MPasteWidget.*`：主窗口与主要交互入口。
- `MTextBrowser.*`：用于卡片正文展示的轻量文本浏览控件。
- `ScrollItemsWidget.*`：横向滚动的条目列表容器。
- `ToggleSwitch.*`：设置界面中使用的开关控件。
- `WebLinkThumbWidget.*`：链接预览卡片。

## 维护约定

- 修改 `.ui` 文件时，保持对应的 `.h/.cpp` 行为一致。
- 修改卡片尺寸、滚动策略或窗口结构时，同时检查主窗口布局与交互体验。
- 若本目录结构或职责发生变化，请同步更新本 README。

## Recent Notes

- `ClipboardItemWidget` 现在支持上下文菜单中的“纯文本粘贴”动作。
- `ScrollItemsWidget` 会把纯文本粘贴请求继续转发给主窗口。
- `ClipboardItemDetailsDialog` 现在提供更完整的条目检查视图，可查看归一化结果与原始 MIME 数据。
- `ClipboardItemInnerWidget` 现在支持通过提取 HTML 中的 `<img src>` 来加载 WPS / 金山图片预览。
- `MPasteWidget` 现在会在短时间内抑制重复提示音，减少一次复制触发多次响声的问题。
- `MPasteWidget` 现在会在每次播放提示音前按当前默认输出设备重建播放链路，减少运行中切换耳机后提示音仍走旧设备的问题，同时避免设备变化回调触发的重复重建。
- `MPasteWidget` 现在会把提示音播放器指针初始化为 `nullptr`，避免重建播放链路时因未初始化指针导致的启动崩溃。
- `WebLinkThumbWidget` 和图片卡片现在使用更饱满的 fill-and-crop 预览方式。
- `ClipboardItemWidget` 现在会为右下阴影预留外层占位，`ScrollItemsWidget` 也会按卡片外框高度计算滚动区，减少卡片底边被截断的视觉问题。
- `ScrollItemsWidget` 现在会在列表左右预留呼吸边距，并在视口边缘绘制接近主窗口淡灰玻璃底色的轻雾渐变遮罩，让横向滚动时更接近贴边淡出的效果。
- 文件类卡片现在也会像链接卡片一样隐藏底部 `infoWidget`，让缩略图区域更完整。
- `FileThumbWidget` 现在移除了默认布局边距，并让图标展示区垂直扩展，减少文件卡片正文与底部之间的空白。
- `ClipboardItemInnerWidget` 现在统一让正文区子控件按可用空间扩展，减少文本、富文本、图片、文件和链接卡片正文与底部信息区之间的空白。
- `ClipboardItemInnerWidget` 现在会收紧 `QTextBrowser` 的上下内边距，并把富文本默认段落外边距归零，减轻正文与底部信息区之间的额外留白。
- 链接预览图、图片卡片和图片文件缩略图现在都优先占满可用高度，只在宽度超出时做左右居中裁剪。
- `WebLinkThumbWidget` 在抓不到网页预览图时，现在会生成一个基于域名配色与字母徽标的兜底封面，不再只是默认图标。
