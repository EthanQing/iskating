# Debugging Runbook

上级入口：[[00-index|AI 知识库索引]]、[[runbooks/README|Runbook 地图]]
相关模块：[[modules/core|应用核心]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]
相关流程：[[flows/video-streaming-flow|视频播放流程]]、[[flows/pose-analysis-flow|运动员检测与身份流程]]、[[flows/training-record-flow|训练记录流程]]
相关风险：[[05-pitfalls|坑点]]

## 常见 bug 排查方法

先按模块缩小范围：

- 启动失败：看 `src/app/main.cpp`, `mainwindow.pro`, 输出目录 DLL/插件。
- 视频不播放：看 `src/ui/videoopenglwidget.cpp`, `src/infrastructure/video/streamregistry.cpp`, `src/infrastructure/video/rtspstream.cpp`。
- AI 无结果：看 `src/application/athleteanalysismanager.cpp`, `src/infrastructure/video/d3dframeextractor.cpp`, `src/infrastructure/inference/tensortrtathletebackend.cpp`。
- 身份异常：看 `src/infrastructure/inference/tensortrtathletebackend.cpp` 的 gallery、阈值、人工绑定和 track 逻辑。
- 配置丢失：看 `MainWindow::loadCameraSettings()` 和 `persistSystemSettings()`。

## 日志位置

代码使用 `qDebug()` 和 `qWarning()`。当前未发现统一日志文件路径。

TODO: 未确认发布包中日志如何收集。`x64/Release` 中曾出现 `uia-enum-*.log`，但这看起来不是项目统一日志。

## 网络请求排查

- RTSP URL 由 `composeCameraUrl()` 生成。
- 日志会用 `safeUrlForLog()` 遮蔽密码。
- `RtspStream` 先尝试 UDP，失败后尝试 TCP。
- FFmpeg open/read 错误会进入状态文本，并记录断流时间、重连次数、恢复时间和最近错误。
- 断流重连日志使用 `[RtspStream]` 前缀：`open attempt` 表示一次打开尝试；`udp failed, retry tcp` 表示 UDP fallback；`stream interrupted` 表示打开或读取失败；`reconnect scheduled` 表示已排队下次重连；`long outage` 表示断流超过 30 秒；`stream recovered` 表示恢复成功。
- 现场排查时优先看 UI 状态：`断流重连中` 通常是网络/摄像头临时不可达；`长时间断流` 需要检查摄像头供电、网络、RTSP 服务、用户名密码、端口和码流路径；`D3D11VA` 或 `硬解` 错误优先排查编码兼容和 GPU 驱动。

相关文件：

- `src/ui/mainwindow.cpp`
- `src/ui/videoopenglwidget.cpp`
- `src/infrastructure/video/rtspstream.cpp`

## 数据库排查

训练数据位于 FastAPI/PostgreSQL，摄像头和采集偏好仍排查 QSettings：

- 检查 `QApplication::setApplicationName("iSkating Coach")` 和 organization `iSkating`。
- 检查 `cameraDefaults/*`, `capture/*`, `cameras/cameraXX/*`, `trainingHistory`。
- 如需重置配置，优先通过系统设置或明确备份后操作，不要在代码里硬删用户配置。

相关文件：

- `src/app/main.cpp`
- `src/ui/mainwindow.cpp`

## 认证问题排查

客户端没有用户可见登录页，但 `TrainingRepository` 访问 FastAPI 时会使用已有 token，或用配置的账号密码自动登录；服务端使用 JWT，worker 使用独立服务令牌。先区分训练服务认证与 RTSP 认证。RTSP 认证问题通常来自：

- 用户名/密码错误。
- 密码中特殊字符 URL 编码问题。
- 端口或路径错误。
- 摄像头禁用 RTSP 或限制码流。

相关文件：

- `src/ui/systemsettingsdialog.cpp`
- `src/ui/mainwindow.cpp`

## 模型问题排查

- 模型文件不存在会在 `TensorRtRunner::initialize()` 返回错误。
- `.fp16.engine` 反序列化失败时，尝试删除本机 engine 后重新构建。
- PersonViT 缺失时 YOLO26x 检测仍可继续；YOLO26x 缺失时视频播放仍可继续但 AI 停止。
- 使用 `python tools/check_athlete_models.py` 检查 ONNX SHA256；TensorRT engine 与目标 GPU 绑定，失败时删除同目录 `.fp16.engine` 后重建。

相关文件：

- `src/infrastructure/inference/tensorrtrunner.cpp`
- `src/infrastructure/inference/tensortrtathletebackend.cpp`
- `models/athlete/athlete_models.json`
