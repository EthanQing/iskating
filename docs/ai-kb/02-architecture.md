# Architecture

上级入口：[[00-index|AI 知识库索引]]
相关文档：[[01-project-overview|项目概览]]、[[04-conventions|代码约定]]、[[05-pitfalls|坑点]]
相关模块：[[modules/core|应用核心]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]、[[modules/background-workers|后台线程]]
相关流程：[[flows/video-streaming-flow|视频播放流程]]、[[flows/pose-analysis-flow|运动员检测与身份流程]]、[[flows/training-record-flow|训练记录流程]]

## 前端架构

项目是 Qt Widgets 桌面应用，不是 Web 前端。UI 由 `mainwindow.ui` 定义基础布局，`mainwindow.cpp` 在运行时替换或动态装配部分组件。

关键文件：

- `mainwindow.ui`: 主窗口页面、12 路摄像头、历史页和建议页的 Designer 布局。
- `mainwindow.cpp`: 页面切换、按钮行为、全屏/侧栏/轨迹三态、动态训练记录卡片。
- `styles/iskating.qss`: 暗色训练仪表盘 QSS。
- `iskating.qrc`: 注册 `styles/`, `images/`, `icons/` 到 Qt 资源系统。
- `videoopenglwidget.cpp`: 视频控件、占位状态、播放控制浮层。
- `d3dvideosurface.cpp`, `videoopenglwidget.cpp`: 运动员检测框和身份标签覆盖层。

## 后端架构

系统是三层架构：

- Windows Qt/C++ 客户端：`RtspStream` 负责 FFmpeg/D3D11VA 解码与重连；`AthleteAnalysisWorker` 从活动流抽帧并调用 YOLO26x/PersonViT；`AnalysisTaskManager` 用单并发后台队列处理单视频探测和完整分析轮询。
- FastAPI/PostgreSQL 服务：`server/app/main.py` 提供认证、人员、比赛、标准、session、复核、报告指标、通用任务和完整分析 REST API；`server/app/schema.py` 定义开发期 PostgreSQL schema。
- Ubuntu/DeepStream worker：`analysis_worker/` 从服务端领取租约，对 12 路 NAS 视频逐解码帧做 person/ReID，将 gzip JSONL 分块原子写入 NAS，PostgreSQL 只保存索引与运行状态。

相关文件：

- `rtspstream.cpp`
- `athleteanalysismanager.cpp`
- `analysistaskmanager.cpp`
- `tensorrtrunner.cpp`
- `server/app/main.py`
- `analysis_worker/worker.py`

## 数据层架构

当前数据层分为桌面端本地设置和训练业务服务端：

- Qt `QSettings`: 摄像头公共配置、每路摄像头 URL、采集偏好、训练服务地址和访问令牌，继续兼容旧 key。
- FastAPI 服务端: `server/app/main.py` 提供训练业务 REST API、基础登录和 seed。
- PostgreSQL: 训练业务与分析协议数据，开发期 schema 由 `server/app/schema.py` 维护并通过 `tools/reset_postgres_schema.py` 空库重建，保存账号、人员/ReID 样本元数据、比赛/标准、计划任务、通用/完整分析任务、session/参与者/视频、检测兼容摘要、轨迹/速度/关节指标、旧动作/人工复核和基线。ReID 样本图片和完整分析逐帧分块分别位于文件根目录/NAS，数据库只保存路径或索引。

`TrainingRepository::open()` 不再创建本机 SQLite；它读取 `server/baseUrl` 和 `auth/accessToken`，通过 QtNetwork 连接训练服务。旧 `%APPDATA%/iSkating/iSkating Coach/iskating.db` 仅由 `tools/import_sqlite_to_postgres.py` 在切换前一次性导入 PostgreSQL。

相关文件：

- `trainingdomain.h`
- `trainingrepository.cpp`
- `personmanagementdialog.cpp`
- `mainwindow.cpp`
- `systemsettingsdialog.cpp`
- `main.cpp`

## 认证/权限架构

训练服务提供账号密码登录和 JWT bearer token，worker API 使用独立 token。桌面端读取已保存 token，或使用环境账号自动登录；当前没有可见登录页或账号管理页。用户记录虽有 role，业务路由没有按 role 做 RBAC 授权。

RTSP 摄像头认证仍来自 URL 中的用户名/密码，本机配置保存在 QSettings，日志输出必须脱敏。

相关文件：

- `trainingrepository.cpp`: 登录、bearer token 与 API 调用
- `server/app/main.py`: JWT 与 worker token 验证
- `systemsettingsdialog.h`: `SharedCameraSettings.username/password`
- `mainwindow.cpp`, `videoopenglwidget.cpp`, `rtspstream.cpp`: `safeUrlForLog()`

## 外部服务依赖

- RTSP 摄像头/视频源：`videoopenglwidget.cpp`, `rtspstream.cpp`
- FFmpeg 动态库和开发包：`mainwindow.pro`
- Qt 6.7.3 MSVC 2022 x64 SDK：`mainwindow.pro`
- TensorRT 10.1、CUDA 11.8：`mainwindow.pro`, `tensorrtrunner.cpp`
- YOLO26x 和 TransReID 模型下载/转换：`tools/download_athlete_models.ps1`, `tools/convert_personvit_msmt17.py`
- FastAPI 训练服务与 PostgreSQL：`server/`
- NAS `nas://` 源、视频和结果分块：`server/app/analysis_artifacts.py`, `analysis_worker/`
- Ubuntu 24.04、NVIDIA DeepStream 9、Docker 和 NVIDIA Container Toolkit：`analysis_worker/compose.yml`

## 主要数据流

1. 用户在系统设置中填写公共 RTSP 参数和 12 路相机 IP。
2. `MainWindow::persistSystemSettings()` 将配置写入 `QSettings`。
3. 点击开始采集后，12 路小窗接入预览码流，主视图接入第一路主码流。
4. `VideoOpenGLWidget` 通过 `StreamRegistry` 复用或创建 `RtspStream`。
5. `RtspStream` 输出 `D3DFrame`；`D3DVideoSurface` 负责显示。
6. `AthleteAnalysisManager` 订阅活动相机流，将最新帧转成 RGB。
7. `TensorRtAthleteBackend` 做 YOLO26x person 检测、PersonViT embedding、gallery 匹配和 per-camera track。
8. `MainWindow` 将选中机位结果叠加到主视频并更新身份标签；人工绑定可覆盖当前 track 的低置信度身份。已识别且已四点标定的机位会同时计算二维轨迹与速度。
9. 保存训练后，桌面端通过 FastAPI 提交 session、参与者、检测/身份摘要、视频引用和轨迹/速度。当前主运动员 participant UUID 在客户端与服务端可能不一致，轨迹/速度存在静默漏存风险。
10. 单视频导入先在后台做媒体/D3D11VA 探测和任务登记；导入完成不等于全视频 AI 逐帧分析完成。
11. 12 路完整分析把 `nas://` 批次提交给 DeepStream worker，结果写 NAS 分块并显式激活；完成本身不会自动创建历史 session。
12. 历史页读取当前检测/轨迹与旧动作/评分/姿态兼容数据，并提供复核、报告与趋势。

相关文件：

- `systemsettingsdialog.cpp`
- `mainwindow.cpp`
- `videoopenglwidget.cpp`
- `streamregistry.cpp`
- `rtspstream.cpp`
- `d3dvideosurface.cpp`
- `athleteanalysismanager.cpp`
- `tensortrtathletebackend.cpp`

## 模块关系

- `MainWindow` 是编排中心，直接持有 UI、`AthleteAnalysisManager`、训练记录和摄像头设置。
- `VideoOpenGLWidget` 只管理单个视频源的 UI 状态和活动流，不直接写入全局设置。
- `StreamRegistry` 通过 URL 复用 `RtspStream`，避免同一 URL 被重复解码。
- `TensorRtAthleteBackend` 依赖 `TensorRtRunner`，组合 YOLO26x 检测、PersonViT ReID 和 gallery 匹配。
- `TrainingRepository` 是训练数据边界，所有人工复核、动作标准编辑、session 汇总重算和基线刷新都应通过它完成。

## 架构关键点

- `main.cpp` 必须在 `QApplication` 构造前设置本地 Qt 插件和运行库搜索路径。
- `mainwindow.pro` 强制使用 `C:/Qt/6.7.3/msvc2022_64` 下的 qmake，否则直接报错。
- 视频解码强依赖 D3D11VA；如果解码器不支持 D3D11VA，`RtspStream` 会进入致命错误。
- TensorRT engine 会按 ONNX 文件名生成到同目录的 `.fp16.engine`，首次启动可能很慢。
- 旧姿态文件仍可用于历史数据兼容，但不属于当前实时主流程。
- 任务中心只能恢复展示重启前的未完成任务，不会重建 job 参数，不能直接继续。
- 完整分析的模型/gallery 字段不是冻结快照；激活前校验也尚未覆盖全局 frameIndex 无 gap、PTS 跨块单调和内容/元数据逐项一致。
