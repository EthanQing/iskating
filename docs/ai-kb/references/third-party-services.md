# Third-Party Services

上级入口：[[00-index|AI 知识库索引]]、[[references/README|Reference 地图]]
相关模块：[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]
相关 Runbook：[[runbooks/local-development|本地开发]]、[[runbooks/deployment|部署]]、[[runbooks/debugging|调试]]
相关 API：[[external-apis|外部 API]]

## 数据库服务

未发现外部数据库服务。当前使用本机 `QSettings`。

相关文件：

- `mainwindow.cpp`

## 认证服务

未发现应用级认证服务。RTSP 摄像头可能需要用户名/密码。

相关文件：

- `systemsettingsdialog.h`
- `mainwindow.cpp`

## 邮件服务

TODO: 未发现邮件服务。

## 支付服务

TODO: 未发现支付服务。

## 存储服务

未发现云存储服务。模型文件和训练历史都在本机。

相关文件：

- `models/body/`
- `models/hand/`
- `mainwindow.cpp`

## 日志/监控服务

TODO: 未发现日志收集或监控服务。代码使用 `qDebug()` 和 `qWarning()`。

## 主要第三方 SDK/运行时

- Qt 6.7.3 MSVC 2022 x64：`mainwindow.pro`
- FFmpeg shared：`mainwindow.pro`, `rtspstream.cpp`
- Direct3D 11 / DXGI / D3DCompiler：`mainwindow.pro`, `d3dvideosurface.cpp`
- TensorRT 10.1：`mainwindow.pro`, `tensorrtrunner.cpp`
- CUDA 11.8：`mainwindow.pro`, `tensorrtrunner.cpp`
- ONNX 模型：`models/body/`, `models/hand/`
