# 系统架构

[返回索引](00-index.md) · [项目概览](01-project-overview.md) · [工程约定](04-conventions.md)

## 运行拓扑

```text
RTSP / 本地文件
       │
       ▼
Windows Qt 客户端 ──HTTP/JWT──> FastAPI ──SQL──> PostgreSQL
       │                           │
       │                           ├──文件──> ReID gallery root
       │                           │
       └──nas:// 回放映射          └──索引──> NAS gzip JSONL 分块
                                                ▲
                                                │ worker token / lease
                                      Ubuntu DeepStream worker
                                                ▲
                                                └──12 路 NAS 视频
```

## 组件职责

### Windows 桌面客户端

- `MainWindow` 是 UI 与业务编排中心。
- `VideoOpenGLWidget`/`RtspStream` 负责视频显示和播放控制。
- `AthleteAnalysisManager` 在单独线程中轮询活动流并执行 Windows 实时 AI。
- `AnalysisTaskManager` 用单独线程和独立 `TrainingRepository` 管理单视频与完整分析任务。
- `TrainingRepository` 通过同步 QtNetwork 调用 FastAPI；不得从 UI 线程执行新的长耗时批量请求。
- QSettings 保存摄像头、采集、存储、NAS 映射和服务访问参数。

### FastAPI 训练服务

- 提供登录、人员/ReID、比赛/标准、任务、session、历史、复盘和指标 API。
- 通过 SQLAlchemy Core 风格 SQL 访问 PostgreSQL。
- 启动时只 seed 缺失的默认数据，不创建 schema。
- 普通业务 API 使用 bearer JWT；worker API 使用 `X-Analysis-Worker-Token`。
- 当前有 `role` 与 `require_admin()` helper，但业务路由未形成完整 RBAC。

### DeepStream worker

- 领取带租约的 run，生成临时 gallery 文件并启动原生 C++ 管线。
- 对每个解码帧运行 YOLO；NvDCF 维护单机位 track；按策略运行 PersonViT。
- 分块先完整写入再原子提交，Python worker 计算 SHA256 后登记索引。
- 心跳发现取消请求时终止子进程。

## 关键数据边界

| 数据 | 权威位置 | 说明 |
|---|---|---|
| 摄像头与本机偏好 | QSettings | 包含 RTSP 凭据；需保护和脱敏 |
| 人员、训练、任务、索引 | PostgreSQL | 桌面端只能经 FastAPI 访问 |
| ReID 样本图片 | `ISKATING_IDENTITY_GALLERY_ROOT` | PostgreSQL 保存元数据和 embedding |
| 完整帧率逐帧结果 | NAS gzip JSONL | PostgreSQL 只保存 run/source/chunk 索引 |
| 模型二进制/engine | 部署机文件系统 | ONNX 和 engine 不作为 Git 源码交付 |

## Windows 实时数据流

1. `MainWindow` 从 QSettings 生成视频源并启动控件。
2. `StreamRegistry` 按 URL 复用 `RtspStream`；本地 seek 文件使用独立流。
3. `RtspStream` 后台解码为 D3D11 frame；`D3DVideoSurface` 显示。
4. `AthleteAnalysisManager` 在活动流中 round-robin 取新帧。
5. `D3DFrameExtractor` 转 RGB；`TensorRtAthleteBackend` 执行 YOLO → ROI → track → 按需 ReID。
6. queued callback 回到 UI；`MainWindow` 更新叠加层和约 200ms 的兼容摘要。
7. 已识别且已四点标定时，bbox 底边中点投影为场地点，并计算速度。
8. 保存时通过 FastAPI 提交 session、参与者、视频资产、摘要和条件式指标。

详见 [实时训练流程](flows/realtime-training.md)。

## 完整分析数据流

1. 客户端创建 12 路 `nas://` batch 和 run。
2. worker 领取 run，按源 PTS、源开始偏移和人工校正建立 batch 时间。
3. 原生管线生成每帧对象结果；每约 10 秒形成 gzip JSONL 分块。
4. worker 计算 SHA256 并幂等登记 chunk。
5. 客户端查询进度和范围结果；缺口处必须清空旧叠加。
6. run 通过现有检查后由用户显式激活；激活不会自动创建训练 session。

详见 [完整帧率分析流程](flows/fullrate-analysis.md)。

## 线程与生命周期

- QWidget 只在 UI 线程操作。
- 每个 `RtspStream` 有解码线程；同 URL 可被多个控件共享。
- Windows 实时 AI 是单个 worker，不是每机位一个模型实例。
- 分析任务队列是单并发 FIFO；线程内创建独立 Repository。
- DeepStream worker 是独立 Linux 进程/容器，不共享 Windows 内存状态。
- `RtspStream::stop()` 和 TensorRT 关闭等待时间较长，修改 stop/析构必须验证阻塞路径。

## 架构约束

- 不让 UI 直接写 SQL、NAS 分块或模型内部状态。
- 不把 `participant_pose_frames` 当作权威二维轨迹；轨迹和速度分别使用 `track_points`、`speed_metrics`。
- 不把 `planned` 视频资产解释为磁盘已有录像。
- 不把 run 上的模型/gallery 标签解释为不可变快照。
- 不把完整分析“暂停轮询”解释为远端 worker 暂停。
