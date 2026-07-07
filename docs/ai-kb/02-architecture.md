# Architecture

上级入口：[[00-index|AI 知识库索引]]
相关文档：[[01-project-overview|项目概览]]、[[04-conventions|代码约定]]、[[05-pitfalls|坑点]]
相关模块：[[modules/core|应用核心]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/pose-analysis|姿态分析]]、[[modules/persistence|本地持久化]]、[[modules/background-workers|后台线程]]
相关流程：[[flows/video-streaming-flow|视频播放流程]]、[[flows/pose-analysis-flow|姿态分析流程]]、[[flows/training-record-flow|训练记录流程]]

## 前端架构

项目是 Qt Widgets 桌面应用，不是 Web 前端。UI 由 `mainwindow.ui` 定义基础布局，`mainwindow.cpp` 在运行时替换或动态装配部分组件。

关键文件：

- `mainwindow.ui`: 主窗口页面、12 路摄像头、历史页和建议页的 Designer 布局。
- `mainwindow.cpp`: 页面切换、按钮行为、全屏/侧栏/轨迹三态、动态训练记录卡片。
- `styles/iskating.qss`: 暗色训练仪表盘 QSS。
- `iskating.qrc`: 注册 `styles/`, `images/`, `icons/` 到 Qt 资源系统。
- `videoopenglwidget.cpp`: 视频控件、占位状态、播放控制浮层。
- `skeletonviewwidget.cpp`, `trajectorywidget.cpp`: 姿态骨架和轨迹可视化。

## 后端架构

没有独立服务端。后端能力以内嵌 C++ 模块和后台线程形式存在：

- `RtspStream` 线程负责打开视频源、FFmpeg 解码和重连。
- `HandAnalysisWorker` 线程负责从活动分析流抽帧、转 RGB、调用 TensorRT 后端；采集中可在参与轨迹的多路相机流之间轮询。
- `TensorRtRunner` 封装 TensorRT engine 构建、加载和推理。

相关文件：

- `rtspstream.cpp`
- `handanalysismanager.cpp`
- `tensorrtrunner.cpp`

## 数据层架构

当前数据层分为桌面端本地设置和训练业务服务端：

- Qt `QSettings`: 摄像头公共配置、每路摄像头 URL、采集偏好、训练服务地址和访问令牌，继续兼容旧 key。
- FastAPI 服务端: `server/app/main.py` 提供训练业务 REST API、基础登录和 seed。
- PostgreSQL: 训练业务数据，开发期 schema 由 `server/app/schema.py` 维护并通过 `tools/reset_postgres_schema.py` 空库重建，保存运动员/教练档案、教练-运动员关系、动作标准、训练计划任务、训练 session、动作实例、人工复核、标准参考视频和个体基线。

`TrainingRepository::open()` 不再创建本机 SQLite；它读取 `server/baseUrl` 和 `auth/accessToken`，通过 QtNetwork 连接训练服务。旧 `%APPDATA%/iSkating/iSkating Coach/iskating.db` 仅由 `tools/import_sqlite_to_postgres.py` 在切换前一次性导入 PostgreSQL。

相关文件：

- `trainingdomain.h`
- `trainingrepository.cpp`
- `personmanagementdialog.cpp`
- `mainwindow.cpp`
- `systemsettingsdialog.cpp`
- `main.cpp`

## 认证/权限架构

项目没有应用登录、用户会话或权限系统。可见的认证只来自 RTSP 摄像头 URL 中的用户名/密码。

相关文件：

- `systemsettingsdialog.h`: `SharedCameraSettings.username/password`
- `mainwindow.cpp`: `composeCameraUrl()`, `safeUrlForLog()`
- `videoopenglwidget.cpp`: `safeUrlForLog()`
- `rtspstream.cpp`: `safeUrlForLog()`

## 外部服务依赖

- RTSP 摄像头/视频源：`videoopenglwidget.cpp`, `rtspstream.cpp`
- FFmpeg 动态库和开发包：`mainwindow.pro`
- Qt 6.7.3 MSVC 2022 x64 SDK：`mainwindow.pro`
- TensorRT 10.1、CUDA 11.8：`mainwindow.pro`, `tensorrtrunner.cpp`
- Hugging Face 模型下载地址：`tools/download_rtmw3d_x.ps1`, `models/body/body_model.json`

## 主要数据流

1. 用户在系统设置中填写公共 RTSP 参数和 12 路相机 IP。
2. `MainWindow::persistSystemSettings()` 将配置写入 `QSettings`。
3. 点击开始采集后，12 路小窗接入预览码流，主视图接入第一路主码流。
4. `VideoOpenGLWidget` 通过 `StreamRegistry` 复用或创建 `RtspStream`。
5. `RtspStream` 输出 `D3DFrame`；`D3DVideoSurface` 负责显示。
6. `HandAnalysisManager` 订阅参与轨迹的相机活动流，将最新帧转成 RGB。
7. `TensorRtBodyPoseBackend` 做 YOLOv8n-pose 2D 推理，并尽量追加 RTMW3D 3D 输出。
8. `MainWindow` 将选中机位结果叠加到主视频并更新骨架；所有机位结果按相机覆盖段进入全场轨迹，评分和动作次数沿用现有实时链路。
9. 保存训练后，桌面端通过 FastAPI 写入 PostgreSQL，记录 AI 原始动作实例和关键帧姿态 JSON；历史页可打开独立复盘校准对话框做本地回放、人工复核、手动新增动作、标准参考视频对比和 Markdown/CSV/PDF 报告导出。

相关文件：

- `systemsettingsdialog.cpp`
- `mainwindow.cpp`
- `videoopenglwidget.cpp`
- `streamregistry.cpp`
- `rtspstream.cpp`
- `d3dvideosurface.cpp`
- `handanalysismanager.cpp`
- `tensorrtbodyposebackend.cpp`

## 模块关系

- `MainWindow` 是编排中心，直接持有 UI、`HandAnalysisManager`、`PoseStandardnessScorer`、训练记录和摄像头设置。
- `VideoOpenGLWidget` 只管理单个视频源的 UI 状态和活动流，不直接写入全局设置。
- `StreamRegistry` 通过 URL 复用 `RtspStream`，避免同一 URL 被重复解码。
- `TensorRtBodyPoseBackend` 依赖 `TensorRtRunner`，并组合 `TensorRtRtmw3dBackend`。
- `PoseStandardnessScorer` 只依赖 `PoseFrameResult`，负责分项评分与反馈文案。
- `TrainingRepository` 是训练数据边界，所有人工复核、动作标准编辑、session 汇总重算和基线刷新都应通过它完成。

## 架构关键点

- `main.cpp` 必须在 `QApplication` 构造前设置本地 Qt 插件和运行库搜索路径。
- `mainwindow.pro` 强制使用 `C:/Qt/6.7.3/msvc2022_64` 下的 qmake，否则直接报错。
- 视频解码强依赖 D3D11VA；如果解码器不支持 D3D11VA，`RtspStream` 会进入致命错误。
- TensorRT engine 会按 ONNX 文件名生成到同目录的 `.fp16.engine`，首次启动可能很慢。
- `HandAnalysisManager` 名称含 Hand，但当前实际接入的是人体姿态后端 `TensorRtBodyPoseBackend`。
