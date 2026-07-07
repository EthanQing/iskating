# 训练记录流程

上级入口：[[00-index|AI 知识库索引]]、[[flows/README|流程地图]]
相关模块：[[modules/persistence|本地持久化]]、[[modules/core|应用核心]]、[[modules/pose-analysis|姿态分析]]
前置流程：[[main-user-flow|主用户流程]]、[[pose-analysis-flow|姿态分析流程]]
相关 Reference：[[references/database-schema|数据持久化结构]]、[[05-pitfalls|坑点]]

## 简介

把一次采集过程中的训练上下文、可选比赛/场次/参赛关系归属、分析来源归属、动作标准版本、动作实例、评分、机位、视频引用、教练批注和反馈通过 FastAPI 保存到 PostgreSQL，并生成历史复盘、报告与建议。摄像头配置仍保存在 `QSettings`。

## 触发条件

- 用户选择运动员、教练、动作标准和训练目标后点击“开始采集”。
- 用户在训练上下文中快速新增人员，或打开“人员管理”“比赛管理”维护完整运动员/教练档案、比赛基础信息、场次/分组和参赛运动员关系。
- 姿态回调完成一次动作计数。
- 用户点击“保存记录”。
- 应用启动时加载历史记录。
- 用户进入历史页、回看训练视频、编辑教练批注、导出训练报告或查看建议页。

## 流程步骤

1. `initializeTrainingRepository()` 连接训练服务，必要时登录并加载服务端 seed 后的人员和动作标准。
2. `installTrainingContextPanel()` 在采集页插入运动员、教练、比赛、场次、动作标准、场地、阶段、目标和目标次数/分数控件，并提供“人员管理”“比赛管理”和快速新增人员入口。
3. `PersonManagementDialog` 通过仓储层维护运动员档案、教练档案和教练-运动员关系；删除人员时只归档，不硬删历史外键。比赛管理对话框通过仓储层维护比赛名称、地点、日期、类型、备注、比赛场次/分组和场次内参赛运动员关系；归档比赛、场次或参赛关系后不影响历史 session 展示。
4. 用户点击开始采集前必须选择运动员和动作标准。
5. AI 回调先由 `PoseStandardnessScorer` 生成通用分项分，再由 `ActionStandardScorer` 按所选动作标准权重和阈值生成动作分与纠错反馈。
6. `ActionRepetitionTracker` 根据动作标准中的膝/髋屈伸阈值、防抖规则识别一次动作，生成 `ActionRepetition`。
7. `tick()` 每秒累加 `m_durationSec`，统计卡展示有效动作数、目标次数、均分、目标分和最好分。
8. 采集页训练上下文可选择主运动员和最多 3 名参与运动员；“轨迹绑定”入口把当前帧的 `cameraId + trackId` 绑定到某个参与者。绑定只补全身份协议，不改变 TensorRT 推理或姿态评分算法。
9. 用户点击保存记录，`saveRecord()` 通过 `TrainingRepository::ensureDailyTask()` 生成或更新今日训练计划/任务，再保存 `training_sessions`、`training_session_participants` 与 `action_repetitions`。session 会记录训练开始时间、可选比赛/场次/参赛关系归属、分析来源类型与引用、主码流、回退码流和机位名称；选择场次时服务端用场次自动带出比赛；选择场次且当前运动员存在 active 参赛关系时桌面端自动写入 `eventAthleteId`。分析来源按比赛优先、导入视频其次、普通训练兜底自动判定。离线视频训练会记录本地视频绝对路径且 `camera=0`；每个动作实例会记录动作片段起止时间、动作级运动员/参与者、轨迹 ID、机位和帧时间，并通过 `session_id` 继承 session 的分析归属。
10. `refreshHistory()` 基于历史页当前筛选和分页结果重新生成历史复盘卡和统计摘要。历史页通过 `TrainingRepository::searchSessions()` 支持运动员、教练、比赛、场次、分析来源、时间、比赛关键词、动作和分数区间组合检索，并可按保存时间、平均分、最佳分、有效动作数或训练时长排序；复盘卡展示训练摘要、比赛、场次/分组、参赛号/道次/成绩/名次、分析归属、视频引用、最好/最差动作、关键错误时间轴、教练批注入口、复盘校准入口和 Markdown/CSV/PDF 报告导出入口。
11. 历史页“动作检索”通过 `TrainingRepository::searchRepetitions()` 跨 session 查询动作实例，支持按 session、人员、比赛/场次、动作、来源、有效性、复核状态、分数区间、训练时间、动作片段时间和错误项关键词筛选；结果表格展示身份状态、轨迹 ID 和机位，可双击定位片段，并可导出当前筛选结果为 CSV/XLSX。
11. `openTrainingReview()` 打开独立 `TrainingReviewDialog`。对话框加载动作明细、主视频、标准参考视频和人工复核表单；点击动作行会定位片段，本地视频支持 seek、慢放、逐帧、关键帧定位和姿态叠加；配置 NVR 回放模板时，RTSP/网络视频会按 session 开始时间和动作片段窗口生成 NVR 回放 URL。
12. 教练在复盘校准中可保存人工有效性、起止时间、总分/分项分、错误项、反馈和备注，或手动新增动作。保存后 `TrainingRepository::recalculateSessionSummary()` 重算 session 汇总和个体基线。
13. `editActionStandard()` 可维护动作标准阈值、权重、目标次数/分数、提示文案和参考视频路径；参考视频也可在复盘对话框中选择并保存。
14. `openSessionVideo()` 优先用 `cameraDefaults/nvrPlaybackTemplate`、session 开始时间、机位 IP 和动作片段窗口生成 NVR RTSP 回放 URL；模板不可用时回退到保存的视频引用。离线视频“定位片段”按 `videoClipStartMs` 请求 seek。
15. `editCoachComment()` 更新 `training_sessions.coach_comment`，并刷新历史和建议。
16. `refreshSuggestions()` 基于最近 session、动作标准和弱项分数生成建议，并通过 `TrainingRepository::trendForRecentDays(7/30)` 展示训练次数、人工优先均分、最佳分、动作完成数和弱项变化摘要。

## 涉及文件

- `mainwindow.h`
- `mainwindow.cpp`
- `personmanagementdialog.cpp`
- `trainingdomain.h`
- `trainingrepository.cpp`
- `trainingreviewdialog.cpp`
- `actionstandardscorer.cpp`
- `posestandardnessscorer.cpp`

## 涉及数据

- `TrainingSession`
- `ActionRepetition`
- `SessionHistoryItem`
- `AthleteProfile`
- `CoachProfile`
- `TrainingTrendWindow`
- `athletes`
- `coaches`
- `coach_athletes`
- `training_sessions`
- `competitions`
- `competition_events`
- `event_athletes`
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
- 保存教练批注失败时会弹窗显示训练服务错误。
- 离线视频文件被移动或删除后，历史复盘仍保留摘要，但回看时会在播放器中显示文件不存在。
- 未配置 NVR 模板时，RTSP/网络视频没有通用 DVR seek 能力，定位片段时只显示片段起点作为人工回看参考。
- 旧记录没有人工复核字段或关键帧姿态 JSON 时会使用 AI 原始数据展示；姿态叠加不可用但回放和复核表单仍应可用。
- 保存人工复核或手动动作失败时应弹窗显示训练服务错误，并保留当前表单内容供用户重试。

## 边界情况

- 历史页每页展示 10 条查询结果，由 `SessionSearchPage.pageSize` 控制；不再预先只保留最近 200 条。空结果会保留筛选栏和分页状态，便于调整条件。
- 训练趋势展示在建议页，最近 7/30 天窗口按 `training_sessions.saved_at` 过滤；动作完成数优先统计 `action_repetitions`，旧数据没有动作明细时退回 session 的 `total_reps`。
- 不做动作类型自动分类；动作类型来自训练上下文手动选择。复盘中可人工新增动作，用于修正漏检。
- 动作实例自动计数沿用膝/髋屈伸启发式，但阈值、防抖和目标分可在动作标准编辑入口维护。
- 停止采集会停止视频和 AI，但保留本次计时与动作实例，便于停止后保存。
- 视频片段只保存引用和时间窗口，不生成物理片段文件；离线视频支持按片段起点 seek、慢放和逐帧，但仍只保存原文件路径，不复制或剪辑视频。NVR 第一版只生成对应时间窗口的 RTSP 回放 URL，不下载录像文件。
- 训练报告支持 Markdown、CSV 明细和 PDF 复盘报告，并展示 session 关联的比赛、场次/分组、参赛信息和分析归属；跨 session 动作检索支持 CSV/XLSX 导出。
