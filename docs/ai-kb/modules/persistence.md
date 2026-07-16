# 本地持久化模块

## F-24/F-25 轨迹点与速度持久化

训练 session 的二维路线写入独立的 `track_points`，速度时序写入 `speed_metrics` 并以轨迹点外键关联。两者分别由 `TrainingRepository::trackPointsForSession()`、`speedMetricsForSession()` 和对应 session API 查询。该表只接受已绑定 participant、已完成四点冰面标定的米制坐标；`participant_pose_frames` 不再承担权威轨迹职责。

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[core|应用核心]]、[[frontend|Qt Widgets 前端]]、[[ai-inference|AI 推理]]
相关流程：[[flows/main-user-flow|主用户流程]]、[[flows/training-record-flow|训练记录流程]]
相关 Reference：[[references/database-schema|数据持久化结构]]、[[references/environment-variables|环境变量]]、[[05-pitfalls|坑点]]

## 作用

负责保存和读取摄像头配置、采集偏好、训练业务数据、比赛/场次/参赛关系、人员档案和 ReID 样本库。摄像头与采集偏好仍使用 Qt `QSettings`；运动员/教练档案、训练记录、样本元数据和 embedding 通过 FastAPI 服务端写入 PostgreSQL，样本图片保存在 `ISKATING_IDENTITY_GALLERY_ROOT` 指向的目录。

## 关键文件

- `main.cpp`: 设置 `QApplication` applicationName 和 organizationName。
- `mainwindow.cpp`: 读写摄像头配置、训练上下文、训练历史/复盘卡、动作标准编辑、训练报告导出和采集偏好。
- `trainingdomain.h`: 训练领域结构，包括运动员、教练、动作标准、训练 session、动作明细和训练趋势窗口。
- `trainingrepository.cpp`: QtNetwork API 客户端，保留原同步仓储接口，负责连接训练服务、登录、人员档案保存/归档、教练-运动员关系维护、训练记录保存、人工复核、手动动作、动作标准保存和趋势统计查询。
- `server/app/main.py`: FastAPI 训练服务，负责认证、seed、PostgreSQL 读写和 session 汇总/个体基线刷新。
- `server/app/schema.py`: 当前开发期 PostgreSQL 完整 schema。
- `tools/reset_postgres_schema.py`: 开发期空库重建工具。
- `tools/import_sqlite_to_postgres.py`: 旧 SQLite 到 PostgreSQL 的一次性导入工具。
- `tools/backfill_video_indexes.py`: 为已有 PostgreSQL 开发库幂等补齐视频索引、离线分析任务字段、多人 participant 结果和旧记录关联。
- `personmanagementdialog.cpp`: 人员管理对话框，读写运动员/教练档案并维护教练可带训运动员关系。
- `personmanagementdialog.cpp`: 运动员 ReID 样本添加、删除和 PersonViT embedding 生成。
- `trainingreviewdialog.cpp`: 复盘校准对话框，读写动作复核字段和动作标准参考视频。
- `systemsettingsdialog.h`: `SharedCameraSettings`, `CameraSlotSettings`, `CapturePreferenceSettings`。
- `systemsettingsdialog.cpp`: 系统设置对话框读写 settings struct。
- `cameraconfigtemplate.h/.cpp`: 摄像头配置 JSON 模板导入/导出、校验和摘要。

## 当前设计

`QSettings` key 主要包括：

- `cameraDefaults/username`
- `cameraDefaults/password`
- `cameraDefaults/port`
- `cameraDefaults/previewPath`
- `cameraDefaults/previewFps`
- `cameraDefaults/mainPath`
- `cameraDefaults/mainFps`
- `cameraDefaults/nvrPlaybackTemplate`
- `capture/modelPrecision`
- `capture/fps`
- `capture/analysisSource`
- `capture/analysisTargetFps`
- `capture/analysisMaxStreams`
- `capture/analysisAutoDegrade`
- `videoStorage/rootDir`
- `videoStorage/capacityLimitGb`
- `videoStorage/retentionDays`
- `cameras/camera01` 到 `cameras/camera12`
- `trainingHistory` 数组

系统设置支持把当前摄像头配置导出为 JSON 模板，也支持从 JSON 模板导入并预览摘要后覆盖设置对话框表单。模板包含公共 RTSP 参数、12 路相机标定和分析流策略；模板只是现场批量配置交换格式，不替代 QSettings；用户点击“保存”后仍由 `persistSystemSettings()` 写入上述 key。

系统设置的“视频存储”区域会读写 `videoStorage/rootDir`、`capacityLimitGb` 和 `retentionDays`。未配置根目录时桌面端使用 `QStandardPaths::AppLocalDataLocation/recordings` 作为视频资产默认根目录；容量阈值默认 50GB，保留天数默认 60 天。

`trainingHistory` 仅作为旧数据兼容来源。新版本启动时不会自动导入本机 SQLite；需要时先使用 `tools/reset_postgres_schema.py --yes` 重建空库 schema，再用 `tools/import_sqlite_to_postgres.py` 将旧 `%APPDATA%/iSkating/iSkating Coach/iskating.db` 导入 PostgreSQL。

PostgreSQL 主要表：

- `athletes`, `coaches`, `coach_athletes`
- `competitions`, `competition_events`, `event_athletes`
- `action_categories`, `action_standards`
- `training_plans`, `training_tasks`
- `training_sessions`, `training_session_participants`, `training_video_files`, `offline_analysis_tasks`, `offline_analysis_batches`, `offline_analysis_runs`, `offline_analysis_run_sources`, `offline_analysis_result_chunks`, `action_repetitions`, `participant_repetitions`, `participant_pose_frames`
- `athlete_action_baselines`

`athletes` 保存运动员档案并用 `active` 做归档；`athlete_identity_samples` 保存 ReID 样本文件路径、文件名、模型版本和预处理版本；`athlete_identity_embeddings` 保存样本对应的 embedding、维度、模型版本和预处理版本。人员删除不会硬删历史外键，只从训练选择与人员管理列表中隐藏。`coaches`、比赛、训练 session 和视频资产继续保留历史兼容字段。`participant_pose_frames` 仍可保存旧姿态时间线，也承载新训练的检测框、trackId、身份状态和置信度；新训练不填充虚假的关键点。`action_repetitions`、`participant_repetitions` 和评分字段只供旧记录或人工复盘兼容使用。

复盘、趋势和报告默认使用“人工优先”的有效值：动作有人工复核时使用人工字段，否则使用 AI 原始字段。保存复核或新增手动动作后，`TrainingRepository::recalculateSessionSummary()` 会重算 `training_sessions` 汇总分、动作数和个体基线。

`loadCameraSettings()` 兼容旧字段：`previewUrl`, `mainUrl`, `url`, `ip`, `port`, `path`。

## 对外接口

摄像头配置仍通过 `MainWindow` 私有方法使用：

- `loadCameraSettings()`
- `saveCameraSettings()`
- `persistSystemSettings()`
- `loadTrainingRecords()`

摄像头模板通过独立 helper 使用：

- `loadCameraConfigTemplate(filePath, cameraCount)`
- `saveCameraConfigTemplate(filePath, shared, capture, cameras, errorMessage)`
- `cameraConfigTemplateSummary(data)`

训练业务数据通过 `TrainingRepository` 使用：

- `open()`
- `athletes()`, `coaches()`, `actionStandards()`
- `competitions()`, `saveCompetition()`, `archiveCompetition()`
- `competitionEvents()`, `saveCompetitionEvent()`, `archiveCompetitionEvent()`
- `eventAthletes()`, `saveEventAthlete()`, `archiveEventAthlete()`
- `athleteIdsForCoach()`
- `saveAthleteProfile()`, `archiveAthlete()`
- `saveCoachProfile()`, `archiveCoach()`
- `ensureDailyTask()`
- `saveOfflineAnalysisTask()`
- `saveTrainingSession()`
- `saveCoachComment()`
- `reviewedRepetitionsForSession()`
- `saveRepetitionReview()`
- `createManualRepetition()`
- `saveActionStandard()`
- `recalculateSessionSummary()`
- `searchSessions()`, `searchRepetitions()`, `recentSessions()`, `repetitionsForSession()`, `trendForRecentDays()`, `baselineFor()`

`searchSessions(filters, page, sort)` 是历史页组合检索入口，按运动员、教练、比赛、场次、参赛关系、分析来源、动作标准、保存时间、分数区间和比赛关键词分页查询 `training_sessions`。比赛关键词匹配 `competitions.name/location/competition_type/notes`、`competition_events.race_name/event_name/heat_name/group_name/notes`、`event_athletes.bib_number/lane_number/notes`、`training_sessions.source_type/source_ref` 以及 `training_sessions.site/training_phase/goal/notes/feedback/coach_comment`。`recentSessions(limit)` 保留兼容，内部按保存时间倒序读取第一页。

`searchRepetitions(filters, page)` 是跨 session 动作实例检索入口，优先查询 `participant_repetitions` 并在旧记录缺失新表结果时回退 `action_repetitions`，支持按 session、人员、比赛/场次、动作、来源、有效性、复核状态、分数区间、训练时间、动作片段时间和错误项关键词检索。人员筛选优先匹配 participant 结果的 `athlete_id`，旧记录没有动作级身份时回退到 session 主运动员。筛选和展示默认使用“人工优先”的有效值；历史页动作明细检索对话框可将当前筛选结果导出为 CSV 或 XLSX。

`poseFramesForSession(sessionId, participantId, athleteId, fromMs, toMs, limit)` 查询 `participant_pose_frames`，供历史页“姿态轨迹”复盘面板按参与者、时间轴、机位和轨迹 ID 查看连续姿态摘要。轨迹复盘同时读取 `speedMetricsForSession()`，按 participant 查看瞬时/平滑速度、有效标记、窗口和算法版本，并可导出 CSV/XLSX；旧 session 没有速度时序时保持空值，不影响轨迹或动作复盘。

`videoFiles(status, withLocalPathOnly, modifiedBefore)` 查询 `training_video_files` 并联查 session、运动员和动作引用数量，供系统设置生成本机视频清理候选列表。桌面端只会把“已登记、路径在配置根目录下、文件实际存在”的视频列为可删除候选；删除本机文件后调用 `markVideoFileCleaned(videoFileId, reason)` 在视频资产 metadata 写入 `cleanupDeletedAt/cleanupReason/cleanupMissing`，不删除训练记录、动作实例或视频资产行，也不扩展 `status` 枚举。

`saveOfflineAnalysisTask(task)` 保留旧单视频导入兼容。完整帧率路径使用 `createOfflineAnalysisBatch()`、`createOfflineAnalysisRun()`、运行控制、范围查询和激活接口。批次固定含 12 路 `nas://` 源；运行源独立保存帧数、PTS、重试与完成状态；结果分块索引保存 URI、时间/帧范围、SHA256 和 schema 版本。

完整逐帧结果不写 PostgreSQL。worker 将 10 秒 gzip JSONL 分块原子写入 `ISKATING_ANALYSIS_NAS_ROOT`，数据库仅保存索引。带完整分析批次的 `saveTrainingSession()` 不再上传全量 `participant_pose_frames`；完成运行激活后，服务端按约 200 ms 从分块派生兼容摘要。视频资产全部标记清理后，服务端删除对应结果文件、保留最小审计信息并把运行/批次归档。

`trendForRecentDays(days)` 用 `training_sessions.saved_at` 做最近 N 天窗口统计，返回训练次数、session 均分、最佳分和动作完成数；动作完成数优先来自 participant 结果数量，旧记录没有新表动作明细时退回 `action_repetitions` 或 session 汇总字段。弱项分项均值优先来自动作实例的人工有效分项分，没有动作实例分项时退回 session 分项分。

## 常见修改任务

### 增加训练记录字段

1. 修改 `trainingdomain.h` 中对应 session 或 repetition struct。
2. 在 `server/app/schema.py`、`server/app/main.py` 的读写逻辑、`trainingrepository.cpp` 的 JSON 映射和导入工具中同步字段。
3. 更新 `mainwindow.cpp` 的保存、历史和建议页展示。
4. 如果字段影响复盘校准，更新 `trainingreviewdialog.cpp` 和报告导出。

### 增加人员档案字段

1. 修改 `trainingdomain.h` 的 `AthleteProfile` 或 `CoachProfile`。
2. 修改 `server/app/schema.py`、FastAPI 读写、`trainingrepository.cpp` JSON 映射和导入工具。
3. 修改 `personmanagementdialog.cpp` 的表格、表单和保存映射。
4. 如果字段会出现在训练记录、报告或建议页，同步更新 `mainwindow.cpp` 的展示逻辑。

### 增加摄像头或分析配置字段

1. 修改 `SharedCameraSettings`、`CameraSlotSettings` 或 `CapturePreferenceSettings`。
2. 修改 `SystemSettingsDialog` UI 和校验。
3. 修改 `cameraconfigtemplate.cpp` 的 JSON 导入/导出映射和校验。
4. 修改 `persistSystemSettings()` 和 `loadCameraSettings()`。
5. 兼容已有 QSettings。

## 注意事项

- QSettings 的物理存储位置由 Qt 和 Windows 决定，代码中未显式指定 ini 文件路径。
- 训练业务依赖 FastAPI + PostgreSQL；桌面端使用 QtNetwork，不再部署 `plugins/sqldrivers/qsqlite(d).dll`。
- 默认服务地址来自 `QSettings server/baseUrl`，没有设置时为 `http://127.0.0.1:8000`；访问令牌保存到 `auth/accessToken`。
- RTSP 密码会被持久化到本机设置。
- 删除或重命名 key 会影响旧用户配置；应保留兼容读取。
- 摄像头配置模板会包含 RTSP 密码，导入确认摘要和日志不得展示明文完整 RTSP URL。
- 不再向 `trainingHistory` 写入新训练记录。
- 复盘校准保存的是主码流/回退码流引用、离线分析任务、session 级视频资产、动作片段到视频资产的索引、动作片段时间窗口、关键帧姿态 JSON 和连续姿态/轨迹摘要。`training_video_files` 当前只登记规范化录像路径和元数据，不会录制、剪辑或复制视频文件；离线回看依赖原文件仍在本机，NVR 回看依赖 `cameraDefaults/nvrPlaybackTemplate` 能按机位和时间生成可访问 RTSP 回放 URL。
- 离线导入现在依赖训练服务先创建 `offline_analysis_tasks`；服务不可用或任务保存失败时不会切换到离线分析源，避免只有本机播放状态、没有数据库任务记录。
- 视频存储清理只删除用户确认勾选的本机文件，并在视频资产 metadata 记录清理信息；训练记录和动作片段索引继续保留，历史回看会提示文件已清理或移动并回退 NVR/RTSP。
- 默认动作标准 seed 只插入缺失项，不应覆盖用户本地编辑的阈值、权重、提示文案或参考视频。
- 人员“删除”是归档：`active=0` 后不再出现在选择列表，但历史训练记录仍保留原外键和姓名联查能力；不要硬删被训练记录引用的人员。
- 模型版本变化后，旧 embedding 不会进入当前 gallery，需要重新生成样本向量。
- 新训练没有 repetition 时不刷新旧动作 baseline，避免把无评分记录混入历史评分统计。

## 相关流程

- `../flows/main-user-flow.md`
- `../flows/training-record-flow.md`

## 未确认问题

- TODO: 未确认是否需要可导入第三方训练系统的专用格式；当前支持 Markdown、CSV 明细、PDF 复盘报告，以及跨 session 动作明细 CSV/XLSX 导出。
- TODO: 未确认 QSettings 中密码是否需要加密。
