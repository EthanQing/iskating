# Third-Party Services

上级入口：[[00-index|AI 知识库索引]]、[[references/README|Reference 地图]]
相关模块：[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]
相关 Runbook：[[runbooks/local-development|本地开发]]、[[runbooks/deployment|部署]]、[[runbooks/debugging|调试]]
相关 API：[[external-apis|外部 API]]

## 数据库服务

训练业务数据使用 PostgreSQL，由 FastAPI 服务端通过 `ISKATING_DATABASE_URL` 访问。桌面端不直连 PostgreSQL。

相关文件：

- `server/app/main.py`
- `server/alembic/versions/20260630_0001_initial_postgresql.py`
- `trainingrepository.cpp`

## 认证服务

FastAPI 训练服务提供基础登录，使用 JWT bearer token。桌面端保存 `auth/accessToken`，也可通过 `ISKATING_API_USERNAME`/`ISKATING_API_PASSWORD` 自动登录。RTSP 摄像头仍可能需要用户名/密码。

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
- QXlsx vendored 源码：`third_party/QXlsx`, `mainwindow.pro`；MIT license，用于动作明细 XLSX 导出。
- ONNX 模型：`models/body/`, `models/hand/`
