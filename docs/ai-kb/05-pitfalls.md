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

离线视频导入也复用这条硬解链路，不是软件解码 fallback；本地文件如果编码不支持 D3D11VA，仍会播放/分析失败。

历史复盘的精确 seek、慢放、逐帧和关键帧定位只对本地离线视频可用；RTSP/网络视频没有通用 DVR seek 能力，只能打开保存的视频源并提示片段起点供人工参考。离线文件若被移动或删除，复盘摘要仍保留，但回看无法播放，需要恢复原文件或重新导入。

相关文件：

- `rtspstream.cpp`
- `videoopenglwidget.cpp`
- `mainwindow.cpp`
- `d3d11videodevice.cpp`
- `d3dvideosurface.cpp`

## ⚠️ 高风险区域：TensorRT engine 缓存

`TensorRtRunner` 会把 ONNX 构建成同目录 `.fp16.engine`。engine 与 TensorRT/CUDA/GPU/模型输入输出强相关，不应当当成可跨机器复用的源码文件。

相关文件：

- `tensorrtrunner.cpp`
- `.gitignore`
- `models/body/body_model.json`
- `models/hand/hand_model.json`

## ⚠️ 高风险区域：多相机轨迹 P1 边界

当前 12 路相机轨迹还原是 P1 版本：系统设置保存每路相机覆盖的场地起止距离和横向偏移，`TrajectoryWidget` 把人体图像锚点线性映射到对应场地段，再拼成全场轨迹。它不是基于内参/外参、单应矩阵、AprilTag/棋盘格或多视角三角化的真实几何标定。

多路 AI 分析由一个 `HandAnalysisWorker` 在多路流之间 round-robin 处理，不是每路一个 TensorRT worker。12 路同时分析时，每路有效 FPS 会受 GPU、解码、码流分辨率和 `capture/modelPrecision` 档位影响。

相关文件：

- `systemsettingsdialog.cpp`
- `mainwindow.cpp`
- `handanalysismanager.cpp`
- `trajectorywidget.cpp`

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

PostgreSQL schema 由 Alembic 管理，桌面端 `TrainingRepository::open()` 不再建表、补列或迁移旧数据。新增字段必须同步 Alembic 迁移、FastAPI 读写、Qt JSON 映射和导入工具。

旧 SQLite 数据不会在桌面端启动时自动迁移；切换前必须执行 `tools/import_sqlite_to_postgres.py` 并核对导入数量。

动作标准 seed 只应插入缺失项，不能覆盖用户本地维护的阈值、权重、提示文案或参考视频路径。`saveActionStandard()` 会递增标准版本，复盘参考视频这类编辑也会形成新版本。

人工复核字段采用“人工优先、AI 原始保留”的读取约定。历史页、建议页、报告和基线重算应通过 `TrainingRepository` 的 effective 数据路径，避免直接读 AI 原始分造成展示不一致。

人员档案删除采用 `active=0` 归档，避免破坏 `training_sessions` 中的历史外键。训练选择、人员管理列表和教练绑定关系只显示 active 人员；历史记录仍应保留原人员引用。

相关文件：

- `mainwindow.cpp`
- `personmanagementdialog.cpp`
- `trainingrepository.cpp`
- `trainingdomain.h`

## 认证/权限坑点

训练服务使用 JWT bearer token。桌面端会保存 `auth/accessToken`，开发环境可用 `ISKATING_API_USERNAME`/`ISKATING_API_PASSWORD` 自动登录；生产环境必须修改默认管理员密码和 `ISKATING_JWT_SECRET`。RTSP 用户名/密码仍会存入本机 `QSettings`，并通过 `QUrl` 写入 RTSP URL。日志必须继续脱敏。

相关文件：

- `systemsettingsdialog.h`
- `mainwindow.cpp`

## 构建或部署坑点

- Release 构建才调用 `windeployqt`，Debug 是否完整部署需要本机验证。
- `mainwindow.pro` 会复制 FFmpeg/TensorRT/CUDA DLL、`Qt6PrintSupport.dll` 和 `models/` 到输出目录；SQLite driver 已移除，训练业务依赖外部 FastAPI/PostgreSQL 服务。
- `models/body/rtmw3d-x.onnx` 很大且被 `.gitignore` 忽略，缺失时 RTMW3D 会不可用，但 2D 姿态仍可初始化。
- 如果 `x64/Release/iskating.exe` 正在运行，Release 构建复制 FFmpeg DLL 时会失败并提示文件被占用；先关闭该进程再重新构建。
- 在普通 PowerShell 中可能没有 `nmake`，Release 构建前需要通过 Visual Studio `vcvars64.bat` 初始化 MSVC 环境。

## UI 布局坑点

- 不要在按钮悬浮事件里通过 `setText()` 增删文字来显示提示，Qt 布局会重新计算宽度导致侧栏或顶栏抖动。
- 图标按钮、右侧操作栏、历史卡片操作按钮应使用固定宽高；长说明放 tooltip/statusTip 或固定宽度区域。
- 视频小窗 overlay 文字必须做 elide 或在窄宽度隐藏，否则长机位名/IP 会压住播放控制按钮。
- `sidebarLayout` 的父控件曾经是 `centralwidget`，不能用 `sidebarLayout->parentWidget()` 推断侧栏容器并设置固定宽度；否则会把整个主窗口锁窄。侧栏必须有真实的 `QFrame#sidebar`，折叠逻辑只操作 `ui->sidebar`。

## 测试坑点

TODO: 当前没有测试目录和测试命令。修改核心逻辑后至少应做手动启动、采集、保存记录、复盘校准、导出报告和模型加载验证。
