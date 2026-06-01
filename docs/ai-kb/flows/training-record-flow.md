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
7. 用户点击保存记录，`saveRecord()` 通过 `TrainingRepository::ensureDailyTask()` 生成或更新今日训练计划/任务，再保存 `training_sessions` 与 `action_repetitions`。session 会记录主码流、回退码流和机位名称；离线视频训练会记录本地视频绝对路径且 `camera=0`；每个动作实例会记录动作片段起止时间。
8. `refreshHistory()` 重新生成历史复盘卡和统计摘要。复盘卡展示训练摘要、视频引用、最好/最差动作、关键错误时间轴、教练批注入口、复盘校准入口和 Markdown/CSV/PDF 报告导出入口。
9. `openTrainingReview()` 打开独立 `TrainingReviewDialog`。对话框加载动作明细、主视频、标准参考视频和人工复核表单；点击动作行会定位片段，本地视频支持 seek、慢放、逐帧、关键帧定位和姿态叠加，RTSP/网络视频只打开保存源并显示片段时间提示。
10. 教练在复盘校准中可保存人工有效性、起止时间、总分/分项分、错误项、反馈和备注，或手动新增动作。保存后 `TrainingRepository::recalculateSessionSummary()` 重算 session 汇总和个体基线。
11. `editActionStandard()` 可维护动作标准阈值、权重、目标次数/分数、提示文案和参考视频路径；参考视频也可在复盘对话框中选择并保存。
12. `openSessionVideo()` 仍可用保存的视频引用在主视图快速回看训练视频；离线视频“定位片段”按 `videoClipStartMs` 请求 seek，RTSP/网络视频打开保存源并提示无法自动定位。
13. `editCoachComment()` 更新 `training_sessions.coach_comment`，并刷新历史和建议。
14. `refreshSuggestions()` 基于最近 session、动作标准和弱项分数生成建议，并通过 `TrainingRepository::trendForRecentDays(7/30)` 展示训练次数、人工优先均分、最佳分、动作完成数和弱项变化摘要。

## 涉及文件

- `mainwindow.h`
- `mainwindow.cpp`
- `trainingdomain.h`
- `trainingrepository.cpp`
- `trainingreviewdialog.cpp`
- `actionstandardscorer.cpp`
- `posestandardnessscorer.cpp`

## 涉及数据

- `TrainingSession`
- `ActionRepetition`
- `SessionHistoryItem`
- `TrainingTrendWindow`
- `training_sessions`
- `action_repetitions`
- `video_source`, `video_fallback_source`, `video_clip_start_ms`, `video_clip_end_ms`
- `source`, `review_status`, `manual_*`, `coach_note`, `key_frame_pose_json`
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
- 离线视频文件被移动或删除后，历史复盘仍保留摘要，但回看时会在播放器中显示文件不存在。
- RTSP/网络视频没有通用 DVR seek 能力，定位片段时只显示片段起点作为人工回看参考。
- 旧记录没有人工复核字段或关键帧姿态 JSON 时会使用 AI 原始数据展示；姿态叠加不可用但回放和复核表单仍应可用。
- 保存人工复核或手动动作失败时应弹窗显示 SQLite 错误，并保留当前表单内容供用户重试。

## 边界情况

- 历史页只展示最多 10 条历史记录，见 `kMaxVisibleHistoryItems`，内存中保留最近 200 条用于摘要。
- 训练趋势展示在建议页，最近 7/30 天窗口按 `training_sessions.saved_at` 过滤；动作完成数优先统计 `action_repetitions`，旧数据没有动作明细时退回 session 的 `total_reps`。
- 不做动作类型自动分类；动作类型来自训练上下文手动选择。复盘中可人工新增动作，用于修正漏检。
- 动作实例自动计数沿用膝/髋屈伸启发式，但阈值、防抖和目标分可在动作标准编辑入口维护。
- 停止采集会停止视频和 AI，但保留本次计时与动作实例，便于停止后保存。
- 视频片段只保存引用和时间窗口，不生成物理片段文件；离线视频支持按片段起点 seek、慢放和逐帧，但仍只保存原文件路径，不复制或剪辑视频。
- 训练报告支持 Markdown、CSV 明细和 PDF 复盘报告；Excel 专用格式留后续。
