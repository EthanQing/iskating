# 本地持久化模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[core|应用核心]]、[[frontend|Qt Widgets 前端]]、[[pose-analysis|姿态分析]]
相关流程：[[flows/main-user-flow|主用户流程]]、[[flows/training-record-flow|训练记录流程]]
相关 Reference：[[references/database-schema|数据持久化结构]]、[[references/environment-variables|环境变量]]、[[05-pitfalls|坑点]]

## 作用

负责保存和读取摄像头配置、采集偏好、训练业务数据。摄像头与采集偏好仍使用 Qt `QSettings`；训练动作标准闭环 v1 已迁移到本地 SQLite。

## 关键文件

- `main.cpp`: 设置 `QApplication` applicationName 和 organizationName。
- `mainwindow.cpp`: 读写摄像头配置、训练上下文、训练历史展示和采集偏好。
- `trainingdomain.h`: 训练领域结构，包括运动员、教练、动作标准、训练 session 和动作明细。
- `trainingrepository.cpp`: SQLite 打开、建表、seed、旧 `trainingHistory` 迁移和训练记录保存。
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
- `ensureDailyTask()`
- `saveTrainingSession()`
- `recentSessions()`, `repetitionsForSession()`, `baselineFor()`

## 常见修改任务

### 增加训练记录字段

1. 修改 `trainingdomain.h` 中对应 session 或 repetition struct。
2. 在 `trainingrepository.cpp` 的建表、读取和保存逻辑中同步字段，并考虑迁移默认值。
3. 更新 `mainwindow.cpp` 的保存、历史和建议页展示。

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

## 相关流程

- `../flows/main-user-flow.md`
- `../flows/training-record-flow.md`

## 未确认问题

- TODO: 后续是否需要训练数据导出文件。
- TODO: 未确认 QSettings 中密码是否需要加密。
