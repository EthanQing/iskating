# Pitfalls

- F-24 只能将有效四点单应性标定得到的坐标写入 `track_points`；不能将未标定画面的像素坐标或旧 `participant_pose_frames.field_x/field_y` 伪装为米制轨迹。

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

离线视频导入也复用这条硬解链路，不是软件解码 fallback；导入前会用 FFmpeg/D3D11VA 校验文件、视频轨、时长、seek 能力和首帧硬解，不兼容编码会提前阻止导入。

历史复盘的精确 seek、慢放、逐帧和关键帧定位只对本地离线视频可用；RTSP/网络视频没有通用 DVR seek 能力。配置 NVR 回放模板后，历史页和复盘校准会按训练开始时间、机位 IP 和动作片段窗口生成 NVR RTSP 回放 URL，但 URL 路径、时间格式和 seek 能力都取决于 NVR 厂商实现。离线文件若被移动或删除，复盘摘要仍保留，但回看无法播放，需要恢复原文件或重新导入。

`training_video_files` 只是视频资产规范和反查索引。`planned` 状态不代表磁盘上已经有录像文件；当前版本不会从 RTSP 实际录制、分段或复制视频。回放仍依赖 NVR 模板、保存的 RTSP 引用或离线原文件。

F-10 的 `action_repetitions.video_file_id/video_index` 只是把动作片段时间窗口关联到 session 视频资产。它能提升历史定位和报告反查能力，但不保证 planned 文件已经存在，也不让普通 RTSP 实时流获得通用 seek 能力。

视频存储容量清理只处理已登记、路径位于配置根目录下且本机实际存在的文件。清理前必须用户勾选并二次确认；清理后只删除本机文件并写入 `training_video_files.metadata.cleanupDeletedAt/cleanupReason`，不会删除训练记录、动作实例或未登记文件。清理后的历史记录仍可查看摘要，但本地精确 seek 需要恢复原文件或改用 NVR/RTSP 回看。

离线导入会先向训练服务写入 `offline_analysis_tasks`。如果服务不可用或任务保存失败，导入会被阻止，避免出现 UI 已切换到离线分析但数据库没有可反查任务的状态。

旧 F-15 单视频路径仍保留；新完整帧率路径使用独立批次/运行/运行源/分块索引。跨主机禁止保存 Windows 盘符，必须使用受根目录约束的 `nas://` URI；Windows 回放还需单独配置 `offlineAnalysis/nasRoot` 映射。首期只按时间戳和人工毫秒偏移同步，不做自动视觉对齐。

完整帧率与实时预览不可共用 latest-frame worker。离线管线的队列、mux 和推理不得配置 leaky/drop 或 YOLO interval；性能不足必须背压。只有原子重命名且 SHA256 校验后的完整 gzip JSONL 分块才能登记检查点，临时文件和未登记文件都不属于已完成结果。

不要覆盖激活运行的分块。模型、预处理、gallery 或同步校正变化必须创建新运行，12 路完整性校验成功后再原子激活。`participant_pose_frames` 只是约 200 ms 兼容摘要，不能用于判断逐帧完整性。

相关文件：

- `rtspstream.cpp`
- `videoopenglwidget.cpp`
- `offlinevideoprobe.cpp`
- `mainwindow.cpp`
- `d3d11videodevice.cpp`
- `d3dvideosurface.cpp`

## ⚠️ 高风险区域：TensorRT engine 缓存

`TensorRtRunner` 会把 ONNX 构建成同目录 `.fp16.engine`。engine 与 TensorRT/CUDA/GPU/模型输入输出强相关，不应当当成可跨机器复用的源码文件。

相关文件：

- `tensorrtrunner.cpp`
- `.gitignore`
- `models/athlete/athlete_models.json`
- `models/hand/hand_model.json`

## ⚠️ 高风险区域：多相机轨迹 P1 边界

旧版本 12 路相机轨迹还原是 P1 版本；当前实时主流程不再刷新轨迹。历史轨迹数据仍可按旧结构读取。

多路 AI 分析由一个 `AthleteAnalysisWorker` 在多路流之间 round-robin 处理，不是每路一个 TensorRT worker。默认最多 12 路、每路目标 5 FPS，并在超载时优先拉长非主机位分析间隔；实际有效 FPS仍会受 GPU、解码、码流分辨率、`capture/modelPrecision` 档位和 TensorRT 推理耗时影响。

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

## 数据库/schema 坑点

当前仍处于空库开发阶段，PostgreSQL schema 不使用 Alembic 增量迁移；每次结构变化都更新 `server/app/schema.py`，再用 `tools/reset_postgres_schema.py --yes` 手动重建开发库。该脚本会删除业务表，不能对需要保留数据的库执行。

新增业务表时，必须同时加入 `BUSINESS_TABLES`；该列表决定重建脚本的删除范围，遗漏会使下一次建表因已有表而失败。

`passlib[bcrypt]==1.7.4` 与较新的 `bcrypt` 不兼容，会在默认管理员 seed 阶段失败或打印后端版本异常；服务端依赖固定为 `bcrypt==4.0.1`。

本机离线视频启动采集后，顶部 AI 状态应在一秒内从“已就绪”变为“等待视频帧”或“运行中”。若持续显示等待视频帧，说明播放画面没有进入 AI 帧流，问题不在 FastAPI/PostgreSQL。

桌面端 `TrainingRepository::open()` 不建表、不补列。新增字段必须同步当前 schema、FastAPI 读写、Qt JSON 映射和导入工具。

旧 SQLite 数据不会在桌面端启动时自动导入；需要先重建空库 schema，再执行 `tools/import_sqlite_to_postgres.py` 并核对导入数量。

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
- 模型二进制被 `.gitignore` 忽略，缺失时视频仍可播放；YOLO26x 缺失会停用 AI，PersonViT 缺失会停用身份匹配。
- 如果 `x64/Release/iskating.exe` 正在运行，Release 构建复制 FFmpeg DLL 时会失败并提示文件被占用；先关闭该进程再重新构建。
- 在普通 PowerShell 中可能没有 `nmake`，Release 构建前需要通过 Visual Studio `vcvars64.bat` 初始化 MSVC 环境。

## UI 布局坑点

- 不要在按钮悬浮事件里通过 `setText()` 增删文字来显示提示，Qt 布局会重新计算宽度导致侧栏或顶栏抖动。
- 图标按钮、右侧操作栏、历史卡片操作按钮应使用固定宽高；长说明放 tooltip/statusTip 或固定宽度区域。
- 视频小窗 overlay 文字必须做 elide 或在窄宽度隐藏，否则长机位名/IP 会压住播放控制按钮。
- `sidebarLayout` 的父控件曾经是 `centralwidget`，不能用 `sidebarLayout->parentWidget()` 推断侧栏容器并设置固定宽度；否则会把整个主窗口锁窄。侧栏必须有真实的 `QFrame#sidebar`，折叠逻辑只操作 `ui->sidebar`。

## 测试坑点

Python 单元测试位于 `server/tests`，但 Windows 环境不能替代 DeepStream 原生构建、真实 NVIDIA 解码和 12 路压力测试。Docker engine 未启动或无法访问 NGC 时，`docker compose config` 通过也不代表镜像可构建。逐帧正确性必须用已知帧号视频和分块内容验证，不能只看数据库进度百分比。

## ReID 与模型分发坑点

- YOLO26x 只输出通用 `person`，不能把 YOLO 类别直接配置成运动员姓名；身份必须来自当前 session gallery、人工绑定或 unknown。
- PersonViT embedding 只在模型版本和预处理版本一致时有效。模型升级后旧向量会被查询过滤，需要重新生成样本 embedding。
- `yolo26x.onnx` 和 `personvit_msmt17_vit_base.onnx` 被 `.gitignore` 忽略；发布目录必须额外准备二进制，并用 `tools/check_athlete_models.py` 校验 SHA256。
- 不同机位维护独立 trackId；跨机位只能通过 athleteId 识别为同一运动员，不能假设 trackId 全局唯一。
- ReID 样本文件不放在 Git，服务端文件根目录由 `ISKATING_IDENTITY_GALLERY_ROOT` 控制，数据库只保存路径和元数据。
