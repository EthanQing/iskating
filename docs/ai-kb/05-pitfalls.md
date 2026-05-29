# Pitfalls

上级入口：[[00-index|AI 知识库索引]]
相关文档：[[02-architecture|架构说明]]、[[03-commands|运行命令]]、[[07-open-questions|未确认问题]]
高风险模块：[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]、[[modules/background-workers|后台线程]]
排查入口：[[runbooks/debugging|调试 Runbook]]、[[references/environment-variables|环境变量]]、[[references/third-party-services|第三方服务]]

## ⚠️ 高风险区域：构建环境强绑定

`mainwindow.pro` 强制官方 Qt 6.7.3 MSVC 2022 x64 路径：

- `C:/Qt/6.7.3/msvc2022_64`
- `C:/Program Files/TensorRT-10.1.0.27`
- `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8`

如果本机路径不同，需要通过环境变量覆盖 FFmpeg/TensorRT/CUDA，Qt 路径则当前写死在 `.pro` 中。

相关文件：

- `mainwindow.pro`
- `tensorrtrunner.cpp`

## 不要修改的生成文件

- `Makefile`
- `Makefile.Debug`
- `Makefile.Release`
- `.qmake.stash`
- `x64/`
- `debug/`
- `release/`
- `*.fp16.engine`
- `ui_*.h`, `moc_*.cpp`, `qrc_*.cpp`

相关文件：

- `.gitignore`

## ⚠️ 高风险区域：D3D11VA 硬解

视频播放当前要求 FFmpeg 解码器支持 D3D11VA。`RtspStream` 如果发现解码器不支持 D3D11VA，会进入 fatal error，没有通用软件解码 fallback。

相关文件：

- `rtspstream.cpp`
- `d3d11videodevice.cpp`
- `d3dvideosurface.cpp`

## ⚠️ 高风险区域：TensorRT engine 缓存

`TensorRtRunner` 会把 ONNX 构建成同目录 `.fp16.engine`。engine 与 TensorRT/CUDA/GPU/模型输入输出强相关，不应当当成可跨机器复用的源码文件。

相关文件：

- `tensorrtrunner.cpp`
- `.gitignore`
- `models/body/body_model.json`
- `models/hand/hand_model.json`

## 不要重复实现的工具函数

- RTSP URL 组装和兼容旧配置：`mainwindow.cpp`
- URL 密码脱敏：`safeUrlForLog()` 在 `mainwindow.cpp`, `videoopenglwidget.cpp`, `rtspstream.cpp` 中已有。
- 清空动态布局：`clearLayout()` 在 `mainwindow.cpp`。
- QSS 动态属性刷新：`repolish()` 在 `mainwindow.cpp`。
- TensorRT 输入转换：`imageToNhwcFloat()`, `imageToNchwFloat()` 在 `tensorrtrunner.cpp`。

## 环境变量坑点

- `FFMPEG_ROOT`, `TENSORRT_ROOT`, `CUDA_ROOT` 可覆盖默认 SDK 路径。
- `QT_PLUGIN_PATH` 和 `PATH` 会在 `main.cpp` 中被进程内设置；不要依赖全局环境去修复部署问题。
- `tensorrtrunner.cpp` 仍硬编码添加 TensorRT/CUDA 默认 DLL 路径，路径变化时需同步评估。

## 数据库/迁移坑点

项目没有数据库迁移。历史记录和相机配置使用 `QSettings`，字段变化要兼容旧 key。

相关文件：

- `mainwindow.cpp`

## 认证/权限坑点

没有用户登录。RTSP 用户名/密码会存入本机 `QSettings`，并通过 `QUrl` 写入 RTSP URL。日志必须继续脱敏。

相关文件：

- `systemsettingsdialog.h`
- `mainwindow.cpp`

## 构建或部署坑点

- Release 构建才调用 `windeployqt`，Debug 是否完整部署需要本机验证。
- `mainwindow.pro` 会复制 FFmpeg/TensorRT/CUDA DLL 和 `models/` 到输出目录。
- `models/body/rtmw3d-x.onnx` 很大且被 `.gitignore` 忽略，缺失时 RTMW3D 会不可用，但 2D 姿态仍可初始化。

## 测试坑点

TODO: 当前没有测试目录和测试命令。修改核心逻辑后至少应做手动启动、采集、保存记录和模型加载验证。
