# Third-Party Services

上级入口：[[00-index|AI 知识库索引]]、[[references/README|Reference 地图]]
相关模块：[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]
相关 Runbook：[[runbooks/local-development|本地开发]]、[[runbooks/deployment|部署]]、[[runbooks/debugging|调试]]
相关 API：[[external-apis|外部 API]]

## 数据库服务

训练业务数据使用 PostgreSQL，由 FastAPI 服务端通过 `ISKATING_DATABASE_URL` 访问。桌面端不直连 PostgreSQL。

相关文件：

- `server/app/main.py`
- `server/app/schema.py`
- `tools/reset_postgres_schema.py`
- `src/infrastructure/persistence/trainingrepository.cpp`

## 认证服务

FastAPI 训练服务提供基础登录，使用 JWT bearer token。桌面端保存 `auth/accessToken`，也可通过 `ISKATING_API_USERNAME`/`ISKATING_API_PASSWORD` 自动登录。RTSP 摄像头仍可能需要用户名/密码。

相关文件：

- `src/ui/systemsettingsdialog.h`
- `src/ui/mainwindow.cpp`

## 邮件服务

TODO: 未发现邮件服务。

## 支付服务

TODO: 未发现支付服务。

## 存储服务

未使用云存储服务。模型文件在本机，ReID 样本图片由 `ISKATING_IDENTITY_GALLERY_ROOT` 指向服务端文件目录，训练历史在 PostgreSQL。12 路原始录像和完整分析分块位于 Windows/Ubuntu 共同访问的 NAS；跨主机协议只使用 `nas://` URI。

相关文件：

- `models/athlete/`
- `server/app/main.py`
- `models/hand/`
- `src/ui/mainwindow.cpp`

## 日志/监控服务

TODO: 未发现日志收集或监控服务。代码使用 `qDebug()` 和 `qWarning()`。

## 主要第三方 SDK/运行时

- Qt 6.7.3 MSVC 2022 x64：`mainwindow.pro`
- FFmpeg shared：`build/qmake/dependencies.pri`, `src/infrastructure/video/rtspstream.cpp`
- Direct3D 11 / DXGI / D3DCompiler：`build/qmake/dependencies.pri`, `src/infrastructure/video/d3dvideosurface.cpp`
- TensorRT 10.1：`build/qmake/dependencies.pri`, `src/infrastructure/inference/tensorrtrunner.cpp`
- CUDA 11.8：`build/qmake/dependencies.pri`, `src/infrastructure/inference/tensorrtrunner.cpp`
- NVIDIA DeepStream 9 容器：`analysis_worker/Dockerfile`；部署在 Ubuntu 24.04 分析主机，使用 NVIDIA Container Toolkit 和 NAS bind mount。
- QXlsx vendored 源码：`third_party/QXlsx`, `build/qmake/dependencies.pri`；MIT license，用于动作明细 XLSX 导出。
- Ultralytics YOLO26x：来源为 `https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26x.pt`，AGPL-3.0 或 Enterprise license，导出为 ONNX 后运行。
- TransReID：来源为 `https://github.com/damo-cv/TransReID`，使用官方 MSMT17 ViT-Base baseline checkpoint；遵守仓库许可证和 MSMT17 数据集条款。
- ONNX 模型：`models/athlete/`；输入输出和 SHA256 见 `models/athlete/athlete_models.json`、`models/athlete/athlete_models.sha256`。
