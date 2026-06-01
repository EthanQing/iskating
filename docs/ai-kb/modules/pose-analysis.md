# 姿态分析模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[ai-inference|AI 推理]]、[[frontend|Qt Widgets 前端]]、[[core|应用核心]]、[[persistence|本地持久化]]
相关流程：[[flows/pose-analysis-flow|姿态分析流程]]、[[flows/training-record-flow|训练记录流程]]
相关术语：[[06-glossary|术语表]]

## 作用

定义统一姿态数据结构，绘制骨架/轨迹，并基于姿态关键点生成训练分数和反馈。

## 关键文件

- `poseresult.h`: `PoseFrameResult`, `PoseInstance`, `PoseKeypoint`, skeleton enum。
- `poseresult.cpp`: 不同 skeleton 的连线关系和名称转换。
- `posestandardnessscorer.h`: 通用姿态分数结果结构和 scorer 接口。
- `posestandardnessscorer.cpp`: 关键点、对称、重心、稳定、3D 分项评分。
- `actionstandardscorer.h/.cpp`: 根据所选动作标准重算动作分、生成错误项，并追踪单次动作实例。
- `skeletonviewwidget.cpp`: 骨架视图绘制。
- `trajectorywidget.cpp`: 关键点轨迹历史和 3D 偏移绘制。
- `d3dvideosurface.cpp`: 主视频上的姿态覆盖层。
- `mainwindow.cpp`: 动作计数、评分刷新、历史与建议文案。

## 当前设计

- `PoseFrameResult` 是跨 AI、渲染、评分模块的统一交换格式。
- 主流程使用 `PoseSkeletonType::Body17`。
- RTMW3D 成功后，关键点会带 `point3d` 和 `hasPoint3d`。
- `PoseStandardnessScorer` 维护上一帧，用于计算稳定性。
- `ActionStandardScorer` 复用通用分项分，再按动作标准中的权重、最低分和纠错提示生成 `ActionAssessment`。
- `ActionRepetitionTracker` 通过动作标准中的髋/膝屈伸阈值和防抖时间识别一次动作，并输出动作明细。
- 每次自动识别动作会保留最低分/关键错误帧对应的 `PoseFrameResult`，保存训练记录时序列化为 `ActionRepetition::keyFramePoseJson`，供复盘校准姿态叠加使用。
- 人工复核字段不会覆盖 AI 原始姿态评分；复盘、趋势和报告按“人工优先”读取有效分数，AI 原始分仍用于追溯模型表现。

## 对外接口

- `PoseStandardnessScorer::scoreFrame(frame)`
- `ActionStandardScorer::score(baseResult, standard)`
- `ActionRepetitionTracker::update(frame, assessment, nowMsec, sessionStartMsec, completedRepetition)`
- `ActionRepetitionTracker::bestPoseFrameJson()`
- `poseSkeletonBones(skeletonType)`
- `poseSkeletonTypeName(skeletonType)`
- `poseInstanceKindName(kind)`

## 常见修改任务

### 修改评分权重

1. 阅读 `PoseStandardnessScorer::scoreFrame()`。
2. 调整分项函数或总分权重。
3. 用真实姿态帧手动验证反馈文案是否合理。

### 新增 skeleton 类型

1. 更新 `PoseSkeletonType` in `poseresult.h`。
2. 更新 `poseSkeletonBones()` 和 `poseSkeletonTypeName()`。
3. 更新渲染和评分模块对 keypoint index 的假设。

### 修改动作计数

1. 阅读 `ActionRepetitionTracker::update()`。
2. 确认关键点 index 仍对应 Body17。
3. 调整动作标准库中的 `arm_threshold`, `release_threshold`, `debounce_ms`，而不是写死在 `MainWindow`。
4. 验证保存后的 `action_repetitions` 明细。

### 修改复盘姿态叠加

1. 先确认 `ActionRepetition::keyFramePoseJson` 是否存在；旧记录没有该字段数据时 UI 应禁用或提示姿态叠加不可用。
2. 如果改变 `PoseFrameResult` JSON 结构，需要同步更新 `ActionStandardScorer` 的序列化/反序列化和 `TrainingReviewDialog` 的读取逻辑。
3. 人工新增动作可以没有关键帧姿态 JSON，不能因此影响手动复核、统计或报告导出。

## 注意事项

- 评分和动作计数逻辑强依赖 Body17 keypoint index，替换模型时要重新校验。
- `depthScore()` 依赖 3D 关键点数量；RTMW3D 缺失时分数会受影响。
- `PoseStandardnessScorer` 内部保存上一帧，复用同一 scorer 实例时注意状态延续。
- 当前仍未做动作类型自动分类；必须先在训练上下文中手动选择动作标准。动作实例可由阈值规则自动计数，也可在复盘校准中人工新增或修正。
- 关键帧姿态 JSON 是复盘辅助数据，不是视频帧缓存；它不能替代原视频，也不会复制或裁剪媒体文件。

## 相关流程

- `../flows/pose-analysis-flow.md`
- `../flows/training-record-flow.md`

## 未确认问题

- TODO: 当前动作标准阈值仍是内置启发式，需要真实滑冰视频和教练反馈校准。
- TODO: 自动动作识别和更细的动作阶段拆分尚未实现。
