# 旧姿态数据兼容模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[ai-inference|AI 推理]]、[[persistence|本地持久化]]
相关流程：[[flows/pose-analysis-flow|运动员检测与身份流程]]、[[flows/training-record-flow|训练记录流程]]

## 当前状态

客户端已移除 `PoseFrameResult`、旧姿态推理后端、骨架绘制、姿态轨迹和关键点覆盖层；服务端旧姿态表、旧动作评分字段和相关接口仍保留，供历史数据迁移或其他客户端兼容。当前实时主流程不调用姿态推理、关键点评分或自动动作计数。当前二维轨迹是 bbox 底边中点经四点标定的独立 F-24 链路，仍使用 `src/ui/trajectorywidget.cpp` 绘图。

## 兼容范围

- 服务端仍保留旧 `participant_pose_frames.pose_summary` 和 `key_frame_pose` 数据，但当前客户端不再将其映射为 C++ 姿态结果或绘制关键点。
- 旧 `action_repetitions`、`participant_repetitions` 和评分字段仍可由支持这些字段的历史复盘/报告接口处理。
- 新训练保存检测框、身份状态、身份置信度、cameraId、trackId 和 ReID 相似度摘要，不填充虚假的关键点。

## 不应重新接入实时主流程

不要在新采集流程中重新添加旧 body pose、RTMW3D、姿态身份解析、姿态评分或自动 repetition tracker。`TrajectoryWidget` 仍是当前 F-24 二维轨迹的活跃绘图组件，不得误删，也不应把它当成旧姿态推理已恢复。若未来恢复姿态能力，需要单独设计版本化结果类型和产品开关。
