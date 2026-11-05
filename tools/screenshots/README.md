# tools/screenshots

> README 截图的自动拍摄工具。界面改动后运行一次，`screenshot/` 下的图片就会更新。

- `take.py`：在独立的 dbus 会话里启动无头 GNOME Shell（2880 × 1800，2 倍缩放）和本仓库构建的 MPaste，配置、历史、数据目录都在临时目录里，不碰用户自己的数据。按顺序：复制示例内容 → 静物深色 / 流光深色 / 静物浅色三张面板 → 空格预览长图 → 右键图片卡片打开详情 → 齿轮菜单打开设置并切到“文字识别” → 以未看过引导的配置重启，拍引导的快捷键一步。按键与点击通过 `tests/gnome-wayland/driver` 扩展注入，截图用 GNOME Shell 的 `Shell.Screenshot`，再按窗口位置裁剪并缩为 1×。
  - 用法：`python3 tools/screenshots/take.py [--binary build/MPaste] [--out screenshot]`；`MPASTE_SHOTS_KEEP=1` 保留会话目录便于排查，`MPASTE_SHOTS_DEBUG=1` 在打开设置前多拍一张全屏图。
  - 依赖：gnome-shell、Qt 6 开发文件（现场编译示例程序）、Pillow。
- `sample-client.cpp`：示例程序。以一个真实应用的 desktop 文件 id 运行（如 `org.gnome.Ptyxis`、`firefox_firefox`），GNOME 因此显示该应用的名称和图标；它把 `take.py` 写入 `command.json` 的文字、HTML、图片或文件放到剪贴板。内容全部是虚构的。
