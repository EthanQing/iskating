# 本地持久化模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[core|应用核心]]、[[frontend|Qt Widgets 前端]]、[[pose-analysis|姿态分析]]
相关流程：[[flows/main-user-flow|主用户流程]]、[[flows/training-record-flow|训练记录流程]]
相关 Reference：[[references/database-schema|数据持久化结构]]、[[references/environment-variables|环境变量]]、[[05-pitfalls|坑点]]

## 作用

负责保存和读取摄像头配置、采集偏好、训练业务数据和人员档案。摄像头与采集偏好仍使用 Qt `QSettings`；运动员/教练档案、训练动作标准、训练复盘校准和报告数据使用本地 SQLite，当前 schema v4。

## 关键文件

- `main.cpp`: 设置 `QApplication` applicationName 和 organizationName。
- `mainwindow.cpp`: 读写摄像头配置、训练上下文、训练历史/复盘卡、动作标准编辑、训练报告导出和采集偏好。
- `trainingdomain.h`: 训练领域结构，包括运动员、教练、动作标准、训练 session、动作明细和训练趋势窗口。
- `trainingrepository.cpp`: SQLite 打开、建表、schema v4 增量补列、seed、旧 `trainingHistory` 迁移、人员档案保存/归档、教练-运动员关系维护、训练记录保存、人工复核、手动动作、动作标准保存、session 汇总重算和趋势统计查询。
- `personmanagementdialog.cpp`: 人员管理对话框，读写运动员/教练档案并维护教练可带训运动员关系。
- `trainingreviewdialog.cpp`: 复盘校准对话框，读写动作复核字段和动作标准参考视频。
- `systemsettingsdialog.h`: `SharedCameraSettings`, `CameraSlotSettings`, `CapturePreferenceSettings`。
- `systemsettingsdialog.cpp`: 系统设置对话框读写 settings struct。

## 当前设计

`QSettings` key 主要包括：

- `cameraDefaults/username`
- `cameraDefaults/password`
- `cameraDefaults/port`
- `cameraDefaults/previewPath`
- `cameraDefaults/previewFps`
- `cameraDefaults/mainPath`
- `cameraDefaults/mainFps`
- `capture/modelPrecision`
- `capture/fps`
- `cameras/camera01` 到 `cameras/camera12`
- `trainingHistory` 数组

`trainingHistory` 仅作为旧数据兼容来源。首次打开 SQLite 时会迁移到 `training_sessions`，并记录 `legacy_qsettings_id`，不会删除旧 key。

SQLite 数据库路径：

- `QStandardPaths::AppDataLocation/iskating.db`
- Windows 当前为 `%APPDATA%/iSkating/iSkating Coach/iskating.db`

SQLite 主要表：

- `athletes`, `coaches`, `coach_athletes`
- `action_categories`, `action_standards`
- `training_plans`, `training_tasks`
- `training_sessions`, `action_repetitions`
- `athlete_action_baselines`

`athletes` 保存运动员档案并用 `active` 做归档；`coaches` 保存教练档案、专项、电话、备注并用 `active` 做归档；`coach_athletes` 保存教练可带训运动员关系。人员删除不会硬删历史外键，只从训练选择与人员管理列表中隐藏。`training_sessions` 记录训练上下文、任务/计划归属、分项分、视频源引用、回退视频源、机位名称、反馈、备注和单次训练教练批注。`action_repetitions` 保留 AI 原始起止时间、有效性、总分/分项分、错误项、反馈、关键帧时间和视频片段，同时保存人工复核字段：来源、复核状态、复核教练、复核时间、人工起止时间、人工有效性、人工总分/分项分、人工错误项、人工反馈、教练备注和关键帧姿态 JSON。`action_standards` 保存本地标准参考视频路径、参考动作实例和参考说明，用于复盘中的标准动作对比。

复盘、趋势和报告默认使用“人工优先”的有效值：动作有人工复核时使用人工字段，否则使用 AI 原始字段。保存复核或新增手动动作后，`TrainingRepository::recalculateSessionSummary()` 会重算 `training_sessions` 汇总分、动作数和个体基线。

`loadCameraSettings()` 兼容旧字段：`previewUrl`, `mainUrl`, `url`, `ip`, `port`, `path`。

## 对外接口

摄像头配置仍通过 `MainWindow` 私有方法使用：

- `loadCameraSettings()`
- `saveCameraSettings()`
- `persistSystemSettings()`
- `loadTrainingRecords()`

训练业务数据通过 `TrainingRepository` 使用：

- `open()`
- `athletes()`, `coaches()`, `actionStandards()`
- `athleteIdsForCoach()`
- `saveAthleteProfile()`, `archiveAthlete()`
- `saveCoachProfile()`, `archiveCoach()`
- `ensureDailyTask()`
- `saveTrainingSession()`
- `saveCoachComment()`
- `reviewedRepetitionsForSession()`
- `saveRepetitionReview()`
- `createManualRepetition()`
- `saveActionStandard()`
- `recalculateSessionSummary()`
- `recentSessions()`, `repetitionsForSession()`, `trendForRecentDays()`, `baselineFor()`

`trendForRecentDays(days)` 用 `training_sessions.saved_at` 做最近 N 天窗口统计，返回训练次数、session 均分、最佳分和动作完成数；动作完成数优先来自 `action_repetitions` 数量，旧记录没有动作明细时退回 `training_sessions.total_reps`。弱项分项均值优先来自动作实例的人工有效分项分，没有动作实例分项时退回 session 分项分。

## 常见修改任务

### 增加训练记录字段

1. 修改 `trainingdomain.h` 中对应 session 或 repetition struct。
2. 在 `trainingrepository.cpp` 的建表、`ensureColumn()` 补列、读取和保存逻辑中同步字段，并考虑迁移默认值。
3. 更新 `mainwindow.cpp` 的保存、历史和建议页展示。
4. 如果字段影响复盘校准，更新 `trainingreviewdialog.cpp` 和报告导出。

### 增加人员档案字段

1. 修改 `trainingdomain.h` 的 `AthleteProfile` 或 `CoachProfile`。
2. 修改 `trainingrepository.cpp` 的建表、`ensureColumn()`、读取和保存逻辑。
3. 修改 `personmanagementdialog.cpp` 的表格、表单和保存映射。
4. 如果字段会出现在训练记录、报告或建议页，同步更新 `mainwindow.cpp` 的展示逻辑。

### 增加摄像头配置字段

1. 修改 `SharedCameraSettings` 或 `CameraSlotSettings`。
2. 修改 `SystemSettingsDialog` UI 和校验。
3. 修改 `persistSystemSettings()` 和 `loadCameraSettings()`。
4. 兼容已有 QSettings。

## 注意事项

- QSettings 的物理存储位置由 Qt 和 Windows 决定，代码中未显式指定 ini 文件路径。
- SQLite 使用 `QSQLITE` driver；`mainwindow.pro` 会复制 `plugins/sqldrivers/qsqlite(d).dll`。
- RTSP 密码会被持久化到本机设置。
- 删除或重命名 key 会影响旧用户配置；应保留兼容读取。
- 不再向 `trainingHistory` 写入新训练记录。
- 复盘校准保存的是主码流/回退码流引用、动作片段时间窗口和关键帧姿态 JSON，不会录制、剪辑或复制视频文件；离线回看依赖原文件仍在本机，RTSP 回看仍依赖原视频源可访问且不支持通用自动 seek。
- 默认动作标准 seed 只插入缺失项，不应覆盖用户本地编辑的阈值、权重、提示文案或参考视频。
- 人员“删除”是归档：`active=0` 后不再出现在选择列表，但历史训练记录仍保留原外键和姓名联查能力；不要硬删被训练记录引用的人员。

## 相关流程

- `../flows/main-user-flow.md`
- `../flows/training-record-flow.md`

## 未确认问题

- TODO: 未确认是否需要 Excel 或可导入第三方训练系统的专用格式；当前支持 Markdown、CSV 明细和 PDF 复盘报告。
- TODO: 未确认 QSettings 中密码是否需要加密。
