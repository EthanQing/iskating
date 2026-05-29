# 训练记录流程

上级入口：[[00-index|AI 知识库索引]]、[[flows/README|流程地图]]
相关模块：[[modules/persistence|本地持久化]]、[[modules/core|应用核心]]、[[modules/pose-analysis|姿态分析]]
前置流程：[[main-user-flow|主用户流程]]、[[pose-analysis-flow|姿态分析流程]]
相关 Reference：[[references/database-schema|数据持久化结构]]、[[05-pitfalls|坑点]]

## 简介

把一次采集过程中的训练上下文、动作标准版本、动作实例、评分、机位、视频引用、教练批注和反馈保存到本机 SQLite，并生成历史复盘、报告与建议。摄像头配置仍保存在 `QSettings`。

## 触发条件

- 用户选择运动员、教练、动作标准和训练目标后点击“开始采集”。
- 姿态回调完成一次动作计数。
- 用户点击“保存记录”。
- 应用启动时加载历史记录。
- 用户进入历史页、回看训练视频、编辑教练批注、导出训练报告或查看建议页。

## 流程步骤

1. `initializeTrainingRepository()` 打开 SQLite，执行 schema 建表、seed 默认动作标准，并迁移旧 `QSettings/trainingHistory`。
2. `installTrainingContextPanel()` 在采集页插入运动员、教练、动作标准、场地、阶段、目标和目标次数/分数控件。
3. 用户点击开始采集前必须选择运动员和动作标准。
4. AI 回调先由 `PoseStandardnessScorer` 生成通用分项分，再由 `ActionStandardScorer` 按所选动作标准权重和阈值生成动作分与纠错反馈。
5. `ActionRepetitionTracker` 根据动作标准中的膝/髋屈伸阈值、防抖规则识别一次动作，生成 `ActionRepetition`。
6. `tick()` 每秒累加 `m_durationSec`，统计卡展示有效动作数、目标次数、均分、目标分和最好分。
7. 用户点击保存记录，`saveRecord()` 通过 `TrainingRepository::ensureDailyTask()` 生成或更新今日训练计划/任务，再保存 `training_sessions` 与 `action_repetitions`。session 会记录主码流、回退码流和机位名称；每个动作实例会记录动作片段起止时间。
8. `refreshHistory()` 重新生成历史复盘卡和统计摘要。复盘卡展示训练摘要、视频引用、最好/最差动作、关键错误时间轴、动作明细、教练批注入口和 Markdown 报告导出入口。
9. `openSessionVideo()` 用保存的视频引用在主视图回看训练视频；片段定位 v1 以提示片段起点时间为主，不做播放器 seek。
10. `editCoachComment()` 更新 `training_sessions.coach_comment`，并刷新历史和建议。
11. `refreshSuggestions()` 基于最近 session、动作标准和弱项分数生成建议。

## 涉及文件

- `mainwindow.h`
- `mainwindow.cpp`
- `trainingdomain.h`
- `trainingrepository.cpp`
- `actionstandardscorer.cpp`
- `posestandardnessscorer.cpp`

## 涉及数据

- `TrainingSession`
- `ActionRepetition`
- `SessionHistoryItem`
- `training_sessions`
- `action_repetitions`
- `video_source`, `video_fallback_source`, `video_clip_start_ms`, `video_clip_end_ms`
- `PoseStandardnessResult`
- `ActionAssessment`

## 错误处理

- 训练数据库不可用时不开始标准闭环采集，并在 `saveTipLabel` 显示提示。
- 未选择运动员或动作标准时不开始采集、不保存。
- 训练时长小于等于 0 时不保存，并在 `saveTipLabel` 显示提示。
- 旧历史记录迁移时，时间为空但 id 存在则用 id 时间戳回填。
- 模型精度为空时默认 `balanced`。
- 视频引用为空时，复盘卡会禁用回看/定位片段按钮，并保留历史摘要。
- 保存教练批注失败时会弹窗显示 SQLite 错误。

## 边界情况

- 历史页只展示最多 10 条历史记录，见 `kMaxVisibleHistoryItems`，内存中保留最近 200 条用于摘要。
- v1 不做自动动作识别；动作类型来自训练上下文手动选择。
- v1 的动作实例计数沿用膝/髋屈伸启发式，但阈值、防抖和目标分由动作标准配置。
- 停止采集会停止视频和 AI，但保留本次计时与动作实例，便于停止后保存。
- 视频片段 v1 只保存引用和时间窗口，不生成物理片段文件，不支持慢放/逐帧/自动 seek。
- 训练报告 v1 导出 Markdown 文本，PDF/Excel/CSV 留后续。
