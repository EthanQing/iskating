# 旧姿态数据兼容模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[ai-inference|AI 推理]]、[[persistence|本地持久化]]
相关流程：[[flows/pose-analysis-flow|运动员检测与身份流程]]、[[flows/training-record-flow|训练记录流程]]

## 当前状态

`PoseFrameResult`、旧姿态评分结构、旧动作字段和历史复盘读取代码仍保留，用于读取已有训练记录。当前实时主流程不再调用姿态推理、骨架绘制、姿态轨迹、关键点评分或自动动作计数。当前二维轨迹是 bbox 底边中点经四点标定的独立 F-24 链路，仍使用 `TrajectoryWidget` 绘图。

## 兼容范围

- 旧 `participant_pose_frames.pose_summary` 和 `key_frame_pose` 可以继续读取。
- 旧 `action_repetitions`、`participant_repetitions` 和评分字段可以继续复盘、导出和人工修正。
- 新训练使用同一兼容存储接口保存检测框、身份状态、身份置信度、cameraId、trackId 和 ReID 相似度摘要，不填充虚假的关键点。

## 不应重新接入实时主流程

不要在新采集流程中重新添加 `TensorRtBodyPoseBackend`、`TensorRtRtmw3dBackend`、`PoseIdentityResolver`、`PoseStandardnessScorer` 或自动 repetition tracker。`TrajectoryWidget` 仍是当前 F-24 二维轨迹的活跃绘图组件，不得误删，也不应把它当成旧姿态推理已恢复。若未来恢复姿态能力，需要单独设计版本化结果类型和产品开关。
