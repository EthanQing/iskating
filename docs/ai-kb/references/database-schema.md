# Database Schema

上级入口：[[00-index|AI 知识库索引]]、[[references/README|Reference 地图]]
相关模块：[[modules/persistence|本地持久化]]、[[modules/core|应用核心]]
相关流程：[[flows/training-record-flow|训练记录流程]]、[[flows/main-user-flow|主用户流程]]
相关风险：[[05-pitfalls|坑点]]

## 使用的数据库

项目使用两类持久化：

- Qt `QSettings`: 保存摄像头配置和采集偏好。
- PostgreSQL: 保存训练动作标准闭环与训练复盘校准的运动员/教练档案、教练-运动员关系、比赛基础信息、动作标准、计划任务、训练记录、动作明细、视频引用、人工复核、标准参考视频、教练批注、个体基线和应用用户。

相关文件：

- `mainwindow.cpp`
- `personmanagementdialog.cpp`
- `trainingdomain.h`
- `trainingrepository.cpp`
- `server/app/main.py`
- `server/alembic/versions/20260630_0001_initial_postgresql.py`
- `tools/import_sqlite_to_postgres.py`
- `main.cpp`

## schema 位置

QSettings schema 仍分散在读写代码中：

- `MainWindow::loadCameraSettings()`
- `MainWindow::persistSystemSettings()`
摄像头 JSON 模板 schema 位于 `cameraconfigtemplate.cpp`，作为现场导入/导出交换格式；模板导入后仍写回 QSettings，不新增数据库表或字段。
PostgreSQL schema 由 Alembic 管理，初始迁移为 `20260630_0001_initial_postgresql.py`，比赛实体迁移为 `20260707_0002_competitions.py`。seed 数据在 FastAPI 启动时由 `seed_defaults()` 维护。旧 SQLite 数据通过一次性导入工具迁入，不再由桌面端启动时自动补列或迁移。

## 主要数据结构

### `cameraDefaults`

- `username`
- `password`
- `port`
- `previewPath`
- `previewFps`
- `mainPath`
- `mainFps`
- `nvrPlaybackTemplate`: NVR RTSP 回放 URL 模板，支持 `{user}`、`{password}`、`{ip}`、`{port}`、`{channel}`、`{start}`、`{end}`。

### `capture`

- `modelPrecision`
- `fps`

`modelPrecision` 当前同时用于 UI 选项和 AI 轮询间隔：`fast` 约 100ms，`balanced` 约 66ms，`high` 约 33ms。

### `cameras/cameraXX`

`XX` 为 `01` 到 `12`。

- `name`
- `previewUrl`
- `mainUrl`
- `url`
- `ip`
- `port`
- `path`
- `trajectoryEnabled`
- `role`
- `fieldStartM`
- `fieldEndM`
- `lateralOffsetM`
- `mountHeightM`
- `yawDeg`
- `pitchDeg`
- `qualityNote`
- `compatibilityNote`

P1 轨迹拼接默认把 12 路相机按 5m 一段初始化为 CAM 01: 0-5m 至 CAM 12: 55-60m。`fieldStartM/fieldEndM/lateralOffsetM` 会传给 `TrajectoryWidget` 做全场轨迹线性映射；`mountHeightM/yawDeg/pitchDeg` 先作为机位标定信息保存。

### 摄像头 JSON 模板

模板根对象字段：

- `version`: 当前为 `1`。
- `cameraDefaults`: 对应 `cameraDefaults` 中的公共 RTSP 参数。
- `capture`: 对应 `capture` 中的分析偏好。
- `cameras`: 相机数组，字段对应 `cameras/cameraXX` 的 IP、场地标定和备注。

导入时少于 12 路会按默认场地段补齐，多于 12 路只导入前 12 路。模板可包含 `password`，应作为现场配置文件保护。

### PostgreSQL 连接

- 服务端通过 `ISKATING_DATABASE_URL` 连接 PostgreSQL。
- 桌面端通过 `server/baseUrl` 连接 FastAPI 服务。
- 旧 SQLite 路径 `%APPDATA%/iSkating/iSkating Coach/iskating.db` 仅用于一次性导入。

### PostgreSQL 表

#### `users`

应用登录用户，保存用户名、密码哈希、角色、启用状态和时间戳。当前基础角色为 `admin` 和 `coach`。

#### `athletes`

运动员档案：姓名、编号、年龄组、身高体重、项目类型、技术等级、惯用方向/起跳脚、伤病限制、训练目标和 `active` 归档状态。人员管理中的删除会把 `active` 设为 0，不硬删历史训练记录引用。

#### `coaches`, `coach_athletes`

教练档案和教练-运动员基础关系。`coaches` 保存姓名、编号、专项、电话、备注和 `active` 归档状态；`coach_athletes` 保存当前可带训运动员关系。教练删除同样使用归档方式。

#### `competitions`

比赛基础信息：名称、地点、日期、类型、备注和 `active` 归档状态。训练 session 通过可空 `competition_id` 关联一个比赛；比赛归档后不再出现在新训练选择和历史筛选下拉中，但历史记录仍可通过外键联查展示名称、地点、日期和类型。

#### `action_categories`, `action_standards`

动作类别和动作标准库。首批内置 8 个动作标准：基础外刃滑行、蹬冰伸展、压步重心转换、转体准备姿态、跳跃起跳准备、落冰控制、旋转轴线保持、步法节奏控制。

`action_standards` 保存版本、目标次数/分数、组数、休息时间、膝/髋计数阈值、防抖、分项权重、分项最低分、关键阶段、关键点要求、错误项、纠正提示、本地参考视频路径、参考动作实例和参考说明。

复盘参考字段：

- `reference_video_source`: 本地标准参考视频路径或可打开的视频源。
- `reference_repetition_id`: 可选的参考动作实例 id。
- `reference_notes`: 标准参考说明。

#### `training_plans`, `training_tasks`

按运动员和日期生成本地训练计划；保存动作任务、动作标准版本、目标次数、目标分、组数、休息时间和完成状态。

#### `training_sessions`

保存单次训练 session：运动员、教练、可选比赛、计划/任务、动作标准和版本、训练时间、时长、总动作数、有效动作数、平均/最佳分、机位、模型精度、fps、分项分、场地、阶段、目标、视频源引用、回退视频源、视频机位名称、反馈、备注、教练批注和旧 `QSettings` id。离线视频训练使用 `camera=0` 表示非 RTSP 机位来源。

复盘相关字段：

- `video_source`: 保存时所选主分析机位的主码流或离线视频绝对路径。
- `video_fallback_source`: 保存时所选机位的预览/回退码流引用。
- `video_camera_name`: 保存时的机位显示名；离线模式为“离线视频 · 文件名”。
- NVR 回放不新增字段；历史页和复盘校准使用 `started_at`、`duration_sec`、`camera` 以及 QSettings 中的 `cameraDefaults/nvrPlaybackTemplate` 生成回放 URL，模板不可用时回退到 `video_source`/`video_fallback_source`。
- `coach_comment`: 单次训练教练批注。
- `notes`: 训练备注；当前 UI 的训练情境面板、历史卡片和 Markdown/CSV/PDF 报告都会展示。

#### `action_repetitions`

保存每次动作实例：AI 原始开始/结束时间、有效性、总分、分项分、错误项、反馈、关键帧时间、视频片段时间窗口、人工复核字段和关键帧姿态 JSON。

复盘相关字段：

- `source`: `ai` 或 `manual`，区分自动识别和人工新增动作。
- `review_status`: `unreviewed`, `reviewed`, `adjusted` 等本地复核状态。
- `reviewed_by_coach_id`: 执行复核的教练 id。
- `reviewed_at`: 复核保存时间。
- `manual_start_ms`, `manual_end_ms`: 人工修正后的动作起止时间。
- `manual_valid`: 人工有效性。
- `manual_score_total`: 人工总分。
- `manual_score_breakdown_json`: 人工分项分 JSON。
- `manual_error_tags_json`: 人工错误项 JSON。
- `manual_feedback`: 人工反馈。
- `coach_note`: 动作级教练备注。
- `key_frame_ms`: 当前动作中最低分或关键错误帧的相对训练时间。
- `video_clip_start_ms`: 回看片段起点，当前保存为动作开始前约 1.5 秒；离线视频复盘会用该值请求播放器初始 seek。
- `video_clip_end_ms`: 回看片段终点，当前保存为动作结束后约 1.5 秒。
- `key_frame_pose_json`: 关键帧姿态 JSON。旧记录可为空，UI 应禁用姿态叠加但保留复盘能力。

#### `athlete_action_baselines`

按运动员和动作标准自动维护历史 session 数、平均分和平均有效动作数。

### 旧 `trainingHistory`

数组元素对应 `TrainingRecord`：

- `id`
- `time`
- `duration`
- `actions`
- `score`
- `camera`
- `modelPrecision`
- `fps`
- `detectionScore`
- `symmetryScore`
- `balanceScore`
- `stabilityScore`
- `depthScore`
- `feedback`

新版本不再写入该数组，也不会在桌面端启动时自动读取该数组。需要保留旧训练数据时，先使用旧版本或旧 SQLite 库作为来源，再通过一次性导入流程进入 PostgreSQL。

## 迁移方式

服务端 schema 使用 Alembic：

```powershell
cd server
$env:ISKATING_DATABASE_URL="postgresql+psycopg://iskating:password@127.0.0.1:5432/iskating"
alembic upgrade head
```

旧 SQLite 数据使用一次性导入工具：

```powershell
python tools/import_sqlite_to_postgres.py --sqlite "$env:APPDATA/iSkating/iSkating Coach/iskating.db"
```

相关文件：

- `mainwindow.cpp`
- `server/alembic/versions/20260630_0001_initial_postgresql.py`
- `tools/import_sqlite_to_postgres.py`
- `personmanagementdialog.cpp`

## seed 方式

`TrainingRepository::seedDefaults()` 内置默认运动员、默认教练、动作类别和 8 条动作标准。动作标准使用 `INSERT OR IGNORE` 初始化缺失项，不覆盖用户本地编辑后的阈值、权重、提示或参考视频。

## 查询入口

训练历史通过 `TrainingRepository::searchSessions(filters, page, sort)` 查询，支持运动员、教练、比赛、动作标准、保存时间、平均分区间和比赛关键词组合检索，并返回总数和当前页结果；比赛关键词匹配 `competitions.name/location/competition_type/notes` 以及 `training_sessions.site/training_phase/goal/notes/feedback/coach_comment`。`recentSessions(limit)` 仍保留为兼容入口，内部调用默认查询。动作明细通过 `repetitionsForSession()` / `reviewedRepetitionsForSession()` 查询；人员档案通过 `athletes()`、`coaches()`、`athleteIdsForCoach()` 查询；比赛基础信息通过 `competitions()`、`saveCompetition()`、`archiveCompetition()` 查询和维护；最近 7/30 天趋势通过 `trendForRecentDays()` 聚合 `training_sessions` 和 `action_repetitions` 查询；教练批注通过 `saveCoachComment()` 更新；个体基线通过 `baselineFor()` 查询。

人员管理写入口：

- `saveAthleteProfile(...)`: 新增或更新运动员档案。
- `archiveAthlete(...)`: 归档运动员，保留历史训练记录引用。
- `saveCoachProfile(...)`: 新增或更新教练档案，并重写该教练的可带训运动员关系。
- `archiveCoach(...)`: 归档教练，保留历史训练记录引用。

复盘校准写入口：

- `saveRepetitionReview(...)`: 保存人工复核字段，并按人工起止时间更新片段窗口。
- `createManualRepetition(...)`: 创建人工新增动作。
- `saveActionStandard(...)`: 保存动作标准阈值、权重、提示和参考视频；每次保存会递增版本。
- `recalculateSessionSummary(sessionId)`: 按人工优先值重算 session 汇总和个体基线。

`trendForRecentDays()` 不新增表或列。统计窗口使用 `training_sessions.saved_at`；训练次数、平均分和最佳分来自重算后的 `training_sessions`；动作完成数和弱项分项均值优先来自 `action_repetitions` 的人工有效值，旧数据没有动作明细时退回 session 汇总字段。

相关文件：

- `mainwindow.h`
- `mainwindow.cpp`
- `personmanagementdialog.cpp`
