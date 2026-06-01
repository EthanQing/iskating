# Database Schema

上级入口：[[00-index|AI 知识库索引]]、[[references/README|Reference 地图]]
相关模块：[[modules/persistence|本地持久化]]、[[modules/core|应用核心]]
相关流程：[[flows/training-record-flow|训练记录流程]]、[[flows/main-user-flow|主用户流程]]
相关风险：[[05-pitfalls|坑点]]

## 使用的数据库

项目使用两类本地持久化：

- Qt `QSettings`: 保存摄像头配置和采集偏好。
- SQLite: 保存训练动作标准闭环与训练复盘 v1 的运动员、动作标准、计划任务、训练记录、动作明细、视频引用和教练批注。

相关文件：

- `mainwindow.cpp`
- `trainingdomain.h`
- `trainingrepository.cpp`
- `main.cpp`

## schema 位置

QSettings schema 仍分散在读写代码中：

- `MainWindow::loadCameraSettings()`
- `MainWindow::persistSystemSettings()`
SQLite schema 在 `TrainingRepository::migrate()` 中创建，seed 数据在 `TrainingRepository::seedDefaults()` 中维护。新增列通过 `TrainingRepository::ensureColumn()` 兼容已有本地数据库。

## 主要数据结构

### `cameraDefaults`

- `username`
- `password`
- `port`
- `previewPath`
- `previewFps`
- `mainPath`
- `mainFps`

### `capture`

- `modelPrecision`
- `fps`

### `cameras/cameraXX`

`XX` 为 `01` 到 `12`。

- `name`
- `previewUrl`
- `mainUrl`
- `url`
- `ip`
- `port`
- `path`

### SQLite 路径

- `QStandardPaths::AppDataLocation/iskating.db`
- Windows 当前为 `%APPDATA%/iSkating/iSkating Coach/iskating.db`

### SQLite 表

#### `schema_meta`

- `key`
- `value`

用于记录 `schemaVersion`, `seedVersion`, `legacyTrainingHistoryMigrated`。

#### `athletes`

运动员档案：姓名、编号、年龄组、身高体重、项目类型、技术等级、惯用方向/起跳脚、伤病限制和训练目标。

#### `coaches`, `coach_athletes`

教练档案和教练-运动员基础关系。

#### `action_categories`, `action_standards`

动作类别和动作标准库。首批内置 8 个动作标准：基础外刃滑行、蹬冰伸展、压步重心转换、转体准备姿态、跳跃起跳准备、落冰控制、旋转轴线保持、步法节奏控制。

`action_standards` 保存版本、目标次数/分数、组数、休息时间、膝/髋计数阈值、防抖、分项权重、分项最低分、关键阶段、关键点要求、错误项和纠正提示。

#### `training_plans`, `training_tasks`

按运动员和日期生成本地训练计划；保存动作任务、动作标准版本、目标次数、目标分、组数、休息时间和完成状态。

#### `training_sessions`

保存单次训练 session：运动员、教练、计划/任务、动作标准和版本、训练时间、时长、总动作数、有效动作数、平均/最佳分、机位、模型精度、fps、分项分、场地、阶段、目标、视频源引用、回退视频源、视频机位名称、反馈、备注、教练批注和旧 `QSettings` id。离线视频训练使用 `camera=0` 表示非 RTSP 机位来源。

复盘相关字段：

- `video_source`: 保存时所选主分析机位的主码流或离线视频绝对路径。
- `video_fallback_source`: 保存时所选机位的预览/回退码流引用。
- `video_camera_name`: 保存时的机位显示名；离线模式为“离线视频 · 文件名”。
- `coach_comment`: 单次训练教练批注。

#### `action_repetitions`

保存每次动作实例：开始/结束时间、有效性、总分、分项分、错误项、反馈、关键帧时间和视频片段时间窗口。

复盘相关字段：

- `key_frame_ms`: 当前动作中最低分或关键错误帧的相对训练时间。
- `video_clip_start_ms`: 回看片段起点，当前保存为动作开始前约 1.5 秒；离线视频复盘会用该值请求播放器初始 seek。
- `video_clip_end_ms`: 回看片段终点，当前保存为动作结束后约 1.5 秒。

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

新版本不再写入该数组。首次打开 SQLite 时，如果发现旧数组，会迁移到 `training_sessions` 并保留旧数据。

## 迁移方式

没有独立迁移命令。应用启动时 `TrainingRepository::open()` 会执行建表、复盘字段补列、seed 和旧 `trainingHistory` 迁移。

相关文件：

- `mainwindow.cpp`

## seed 方式

`TrainingRepository::seedDefaults()` 内置默认运动员、默认教练、动作类别和 8 条动作标准。

## 查询入口

训练历史通过 `TrainingRepository::recentSessions()` 查询；动作明细通过 `repetitionsForSession()` 查询；最近 7/30 天趋势通过 `trendForRecentDays()` 聚合 `training_sessions` 和 `action_repetitions` 查询；教练批注通过 `saveCoachComment()` 更新；个体基线通过 `baselineFor()` 查询。

`trendForRecentDays()` 不新增表或列。统计窗口使用 `training_sessions.saved_at`；训练次数、平均分和最佳分来自 `training_sessions`；动作完成数和弱项分项均值优先来自 `action_repetitions`，旧数据没有动作明细时退回 session 汇总字段。

相关文件：

- `mainwindow.h`
- `mainwindow.cpp`
