# 主用户流程

上级入口：[[00-index|AI 知识库索引]]、[[flows/README|流程地图]]
相关模块：[[modules/core|应用核心]]、[[modules/frontend|Qt Widgets 前端]]、[[modules/persistence|持久化与训练服务]]、[[modules/ai-inference|AI 推理]]
后续流程：[[video-streaming-flow|视频播放流程]]、[[pose-analysis-flow|运动员检测与身份流程]]、[[training-record-flow|训练记录流程]]
相关 Runbook：[[runbooks/local-development|本地开发]]、[[runbooks/debugging|调试]]
结构与边界：[[../01-project-overview|项目概览]]、[[../02-architecture|架构说明]]

## 简介

用户先维护人员、ReID 样本、比赛/场次和动作标准，再选择实时 RTSP、本地单视频或 12 路远端完整分析。结果统一回到 session 历史、证据复盘和报告建议。

当前 Qt 客户端仍是“运动员检测 / 训练历史与分析 / 动作纠正与建议”三页；全量盘点中的九区导航是目标态，HTML 只是交互原型。

## 触发条件

- 用户启动应用。
- 用户点击人员管理、系统设置、开始采集、暂停、停止、保存记录或页面导航按钮。

## 流程步骤

1. `src/app/main.cpp` 创建 `QApplication`，设置本地插件/运行库路径，显示最大化 `MainWindow`。
2. `MainWindow` 构造时初始化 UI、加载摄像头配置、加载训练历史、创建 AI 分析管理器。
3. `TrainingRepository` 连接 FastAPI/PostgreSQL，读取已保存 token 或使用环境账号自动登录；客户端没有可见登录页。
4. 用户维护运动员/教练档案、带训关系、ReID 样本、比赛/场次/参赛关系和已有动作标准。
5. 用户在训练上下文选择主运动员与最多 3 名附加参与者、教练、标准、比赛/场次、目标与备注。当前代码强制选择动作标准，但 person/ReID 不使用这些阈值。
6. 实时分支：配置 RTSP、12 路机位、有效四点标定与 AI 策略，开始后显示 12 路预览和主码流。`AthleteAnalysisManager` 输出 bbox、ReID、机位内 track；已识别且已标定的结果还生成二维轨迹/速度。
7. 单视频分支：后台探测文件、seek 和 D3D11VA，完成登记后可播放；只有再点击“开始采集”才跟随播放做 Windows 低帧率 AI。导入任务 100% 不等于 AI 逐帧完成。
8. 完整分析分支：导入恰好 12 路 `nas://` 源并创建 run，Ubuntu DeepStream worker 逐解码帧生成 bbox/track/ReID 分块，用户查看进度、重试/取消并显式激活。它不生成轨迹、速度、姿态、动作或评分，也不会自动创建 session。
9. 任务中心统一展示单视频与完整分析任务。当前进程内可控制；重启后只能恢复显示，不能直接继续。
10. 实时或单视频分析停止后，`saveRecord()` 通过 FastAPI 保存 session、参与者、视频资产、检测/身份摘要和条件式轨迹/速度。当前 participant UUID 映射缺口可导致主运动员轨迹/速度漏存。
11. 历史页组合检索 session，提供视频回看、二维轨迹/速度、教练批注和旧动作/评分兼容复核；客户端不再加载旧姿态关键点覆盖层。
12. 报告页导出 Markdown/CSV/PDF 单次报告和轨迹/速度/关节专项指标；旧评分或人工动作数据才具有技术趋势和纠正建议语义。

## 涉及文件

- `src/app/main.cpp`
- `src/ui/mainwindow.cpp`
- `src/ui/mainwindow.ui`
- `src/ui/personmanagementdialog.cpp`
- `src/ui/systemsettingsdialog.cpp`
- `src/ui/videoopenglwidget.cpp`
- `src/application/athleteanalysismanager.cpp`
- `src/infrastructure/inference/tensortrtathletebackend.cpp`
- `src/application/analysistaskmanager.cpp`
- `src/ui/offlineanalysisdialog.cpp`
- `src/infrastructure/persistence/trainingrepository.cpp`
- `server/app/main.py`
- `analysis_worker/`

## 涉及数据

- `SharedCameraSettings`
- `CameraSlotSettings`
- `CapturePreferenceSettings`
- `AthleteProfile`
- `CoachProfile`
- `TrainingSession`
- `AthleteAnalysisResult`
- `AnalysisTask`, `OfflineAnalysisBatch`, `OfflineAnalysisRun`
- `TrainingSession`, `TrainingSessionParticipant`, `TrainingVideoFile`
- `ActionRepetition` 及服务端历史姿态表兼容数据
- `QSettings` keys: `cameraDefaults/*`, `cameras/cameraXX/*`, `capture/*`
- PostgreSQL 核心表：`athletes`, `coaches`, `competitions`, `action_standards`, `analysis_tasks`, `offline_analysis_*`, `training_sessions`, `training_session_participants`, `participant_pose_frames`, `track_points`, `speed_metrics`, `joint_metrics`

## 错误处理

- 设置对话框校验端口和预览路径，失败时 `QMessageBox::warning()`。
- 摄像头模板导入会先校验端口、预览路径、NVR 占位符和场地段，再显示摘要确认；确认后只覆盖表单，点击保存才写入 QSettings。
- 视频源为空或文件不存在时在视频控件显示状态文本。
- AI 初始化失败时模型状态栏显示错误。

## 边界情况

- 没有配置摄像头 IP 时，主视图显示未配置。
- RTSP 采集开始前至少需要一路 `trajectoryEnabled` 的已配置相机；要产生场地轨迹还必须有有效四点标定。离线视频不需要相机 IP。
- 暂停后清空实时检测/身份和轨迹覆盖，但内存训练摘要与历史数据不受影响。
- 训练时长为 0 时保存记录会提示“暂无可保存的训练记录”。
- 人员管理删除人员采用归档方式；历史训练记录仍会保留原人员引用。
- YOLO 缺失时视频尽量继续播放但无 AI；PersonViT 缺失时仍显示 person bbox，身份为 unknown。当前不存在 RTMW3D/2D 姿态降级链路。
