# Debugging Runbook

上级入口：[[00-index|AI 知识库索引]]、[[runbooks/README|Runbook 地图]]
相关模块：[[modules/core|应用核心]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/pose-analysis|姿态分析]]、[[modules/persistence|本地持久化]]
相关流程：[[flows/video-streaming-flow|视频播放流程]]、[[flows/pose-analysis-flow|姿态分析流程]]、[[flows/training-record-flow|训练记录流程]]
相关风险：[[05-pitfalls|坑点]]

## 常见 bug 排查方法

先按模块缩小范围：

- 启动失败：看 `main.cpp`, `mainwindow.pro`, 输出目录 DLL/插件。
- 视频不播放：看 `videoopenglwidget.cpp`, `streamregistry.cpp`, `rtspstream.cpp`。
- AI 无结果：看 `handanalysismanager.cpp`, `d3dframeextractor.cpp`, `tensorrtbodyposebackend.cpp`。
- 分数异常：看 `posestandardnessscorer.cpp`, `MainWindow::updateActionCounter()`。
- 配置丢失：看 `MainWindow::loadCameraSettings()` 和 `persistSystemSettings()`。

## 日志位置

代码使用 `qDebug()` 和 `qWarning()`。当前未发现统一日志文件路径。

TODO: 未确认发布包中日志如何收集。`x64/Release` 中曾出现 `uia-enum-*.log`，但这看起来不是项目统一日志。

## 网络请求排查

- RTSP URL 由 `composeCameraUrl()` 生成。
- 日志会用 `safeUrlForLog()` 遮蔽密码。
- `RtspStream` 先尝试 UDP，失败后尝试 TCP。
- FFmpeg open/read 错误会进入状态文本。

相关文件：

- `mainwindow.cpp`
- `videoopenglwidget.cpp`
- `rtspstream.cpp`

## 数据库排查

没有数据库。排查 QSettings：

- 检查 `QApplication::setApplicationName("iSkating Coach")` 和 organization `iSkating`。
- 检查 `cameraDefaults/*`, `capture/*`, `cameras/cameraXX/*`, `trainingHistory`。
- 如需重置配置，优先通过系统设置或明确备份后操作，不要在代码里硬删用户配置。

相关文件：

- `main.cpp`
- `mainwindow.cpp`

## 认证问题排查

项目没有应用登录。RTSP 认证问题通常来自：

- 用户名/密码错误。
- 密码中特殊字符 URL 编码问题。
- 端口或路径错误。
- 摄像头禁用 RTSP 或限制码流。

相关文件：

- `systemsettingsdialog.cpp`
- `mainwindow.cpp`

## 模型问题排查

- 模型文件不存在会在 `TensorRtRunner::initialize()` 返回错误。
- `.fp16.engine` 反序列化失败时，尝试删除本机 engine 后重新构建。
- RTMW3D 缺失时状态会显示 3D 模型缺失，但 2D 可以继续。

相关文件：

- `tensorrtrunner.cpp`
- `tensorrtbodyposebackend.cpp`
- `tensorrtrtmw3dbackend.cpp`
