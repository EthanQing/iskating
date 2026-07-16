# 训练记录流程

## F-24/F-25 轨迹点与速度

已绑定 participant 的检测框底边每约 200 ms 经相机四点单应性标定转换为场地米制 `(x,y,0)`，并以 `position_delta` 作为速度来源写入 `track_points`。每个轨迹点还承接 `trajectory_speed_v1` 的瞬时/1000ms 平滑速度、单位、版本和有效标记，服务端以轨迹点外键写入 `speed_metrics`。`participant_pose_frames` 继续仅保存检测/姿态兼容摘要；未标定机位和未知 participant 不产生轨迹点或速度。训练保存时服务端按 session 的 participant 关联轨迹点和速度，历史复盘可按运动员查看并导出速度曲线。

上级入口：[[00-index|AI 知识库索引]]、[[flows/README|流程地图]]
相关模块：[[modules/persistence|本地持久化]]、[[modules/core|应用核心]]、[[modules/ai-inference|AI 推理]]
前置流程：[[main-user-flow|主用户流程]]、[[pose-analysis-flow|运动员检测与身份流程]]
相关 Reference：[[references/database-schema|数据持久化结构]]、[[05-pitfalls|坑点]]

## 简介

训练记录保存训练上下文、视频引用、参与者、运动员检测框、身份状态、机位、trackId 和置信度。新版本不保存姿态评分、动作 repetition 或自动动作计数；旧记录的动作、评分和姿态数据仍可读取。

## 流程步骤

1. `initializeTrainingRepository()` 连接 FastAPI/PostgreSQL，加载人员和当前动作标准兼容数据。
2. `PersonManagementDialog` 维护运动员/教练档案，并为运动员添加、查看、删除 ReID 样本；上传后生成与当前 PersonViT 版本匹配的 embedding。
3. 采集页选择主运动员和最多 3 名 session 参与运动员，`reloadAthleteIdentityGallery()` 只加载这些参与者的有效 gallery。
4. `syncAnalysisStreams()` 为活动视频流创建 `AthleteAnalysisManager` 订阅。
5. AI 回调更新主视频检测框、身份标签和 trackId，并按约 200ms 将结果写入兼容的 `participant_pose_frames` 内存列表。
6. 用户可通过“人工绑定”把当前机位的 trackId 绑定到当前 session 参与者；绑定优先覆盖 unknown 或低置信度身份。
7. `saveRecord()` 保存视频资产、参与者和检测框/身份时间线，`totalReps`、`validReps`、评分字段保持为 0，不创建 repetition。
8. 服务端保存 session 时，如果没有 repetition，不刷新旧动作 baseline；已有旧 baseline 不会被新检测记录污染。
9. 历史页继续读取旧动作和评分复盘；姿态时间线入口可读取旧关键点，也可展示新版本的检测框字段，但不把检测框解释为姿态。

## 样本库接口

- `GET /athletes/{athleteId}/identity-samples`：查询样本元数据。
- `POST /athletes/{athleteId}/identity-samples`：上传 base64 图片并保存文件。
- `POST /athletes/{athleteId}/identity-samples/{sampleId}/embedding`：保存 embedding、维度、模型版本和预处理版本。
- `GET /athletes/{athleteId}/identity-gallery`：按模型版本和预处理版本查询有效 gallery。
- `DELETE /athletes/{athleteId}/identity-samples/{sampleId}`：删除样本、embedding 和文件。

## 错误处理

- 数据库或训练服务不可用时不保存记录，并提示用户。
- YOLO26x 缺失时视频仍播放，但不更新检测结果。
- PersonViT 缺失时继续保存 person 检测，身份状态为 unknown。
- 样本图片无效、模型版本不匹配或 embedding 维度无效时拒绝 gallery 写入。

## 历史兼容

旧记录没有 `participant_pose_frames` 时仍可查看动作复盘和报告；旧姿态字段缺失时不阻止历史记录打开。新训练不会补写伪造姿态、评分或动作数据。
