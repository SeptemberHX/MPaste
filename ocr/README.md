# ocr

> `mpaste-ocr`：MPaste 在 Linux 上的文字识别助手程序。每次识别时由 MPaste 启动，读完一张图就退出，模型只在识别时占用内存。

- `main.cpp`：命令行入口 `mpaste-ocr --models DIR [--model tiny|small] [--threads N] PICTURE`（模型文件名为 `PP-OCRv6_det_<model>.onnx`、`PP-OCRv6_rec_<model>.onnx`），用 Qt 读图（透明部分按白底），把识别出的文字以 UTF-8 输出到标准输出。退出码：0 成功，2 参数错误，3 模型加载失败，4 图片读不了，5 没装 ONNX Runtime。
- `PaddleOcr.*`：识别引擎。运行时用 `dlopen` 加载 ONNX Runtime（`libonnxruntime.so.1`，C++ 接口以 `ORT_API_MANUAL_INIT` 手动初始化），跑 PaddleOCR 的 PP-OCRv6 tiny 模型（检测 1.8 MB + 识别 4.5 MB，Apache-2.0，来自 [RapidOCR](https://github.com/RapidAI/RapidOCR) 的 ONNX 版本）：DB 文字检测 → 把每个框按透视拉正裁出 → CRNN 识别 + CTC 解码 → 按阅读顺序排行（同一水平带的框从左到右，拉丁文字之间补空格）。流程参照 [RapidOcrOnnx](https://github.com/RapidAI/RapidOcrOnnx)（Apache-2.0），为 MPaste 做了简化：
  - 不用方向分类模型（复制来的图片都是正的）；
  - 字典直接从识别模型的元数据 `character` 读取，不另带 txt；
  - 检测时短边缩放到 960（屏幕上的字足够大，检测内存随面积增长），长边不超过 2000，都取 32 的倍数；
  - 长截图按约 1.5 倍宽度的高度分片检测，相邻片重叠 160 px，落在重叠区的行只算一次；
  - 框的外扩直接按矩形计算（面积 × 1.6 / 周长），不需要多边形裁剪库；
  - 不用 OpenCV：缩放与裁切是对四边形做双线性采样（缩小 2 倍以上时每像素多点平均），文字区域用 8 邻域连通块，最小外接矩形用凸包加旋转卡壳，结果与 OpenCV 版在测试图上逐字一致或只差个别字。
- 构建：CMake 找到 ONNX Runtime 的头文件时才构建（`sudo apt install libonnxruntime-dev`），程序只链接 Qt Gui 与 libc，并单独开 `-O2`（像素循环未优化时慢 4–5 倍）；配置时下载两个模型到 `build/ocr-models` 并校验 SHA-256，离线构建可用 `-DMPASTE_OCR_MODELS_DIR=` 指定已有模型的目录。
- 安装：deb 装到 `/usr/bin/mpaste-ocr` 与 `/usr/share/mpaste/ocr/`，ONNX Runtime 只列为 Suggests（连同依赖约 80 MB），不随 MPaste 自动安装；本地安装脚本放在 MPaste 旁边（`mpaste-ocr`、`ocr-models/`）。`--check` 只检查 ONNX Runtime 能否加载，退出码 5 表示没装。`utils/OcrService` 先找它，没装运行时就退回 `tesseract`，两者都没有时提示安装命令；设置页“文字识别”里也显示缺什么和“复制命令”按钮。
- 实测（2 倍缩放下的虚构截图）：一页中英混排 0.3 s，1080 × 4992 的长聊天截图 0.8 s（不含 ONNX Runtime 首次加载），识别进程峰值约 200–380 MB，结束即释放。
- 可选的精确模型：PP-OCRv6 small（30 MB），在设置里下载到 `~/.local/share/MPaste/ocr-models/`（见 `utils/OcrModelDownloader`）。字典 18708 字（汉字 15565，含日文假名），tiny 为 6904（汉字 6174，无假名）；官方识别准确率 81.3% 对 73.5%。在干净的屏幕截图上两者错字相当，small 慢约 3 倍（长截图 2.5 s），峰值内存约 610 MB。
