# 运动员检测与身份流程

上级入口：[[00-index|AI 知识库索引]]、[[flows/README|流程地图]]
相关模块：[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/background-workers|后台线程]]
相邻流程：[[video-streaming-flow|视频播放流程]]、[[training-record-flow|训练记录流程]]

## 简介

采集流由 `AthleteAnalysisManager` 读取最新帧，经过 YOLO26x person 检测和 PersonViT ReID 后，回写检测框、身份标签、trackId 和身份状态。此流程不生成姿态、骨架、轨迹或动作评分。

## 流程步骤

1. `MainWindow::syncAnalysisStreams()` 按主机位优先和 FPS 策略生成活动流。
2. `AthleteAnalysisWorker` 在活动流之间 round-robin 选择新帧。
3. `D3DFrameExtractor` 把 D3D11 帧转换为 RGB `QImage`。
4. `TensorRtAthleteBackend` 运行 YOLO26x，保留 score 不低于 `0.35` 且 classId 为 0 的检测框。
5. 对每个检测框 crop 并 resize 到 `128x256`，运行 PersonViT，得到 768 维 L2 embedding。
6. 在当前 session 最多四名参与者 gallery 中计算余弦相似度；最高分达到 `0.60` 且与第二名差值不小于 `0.05` 时标记 identified。
7. 使用框 IoU 和 `1200ms` TTL 维护 per-camera trackId；session 或参与者变化时清空旧 track；人工绑定在低置信度和 unknown 结果上覆盖身份。
8. `MainWindow` 更新主视频检测框和标签，并按约 `200ms` 采样保存兼容字段到 `participant_pose_frames`。
9. 停止采集后保存视频、参与者、检测框、身份状态、机位、trackId 和置信度；不写入伪造姿态、评分或 repetition。

## 样本库流程

人员管理页上传样本图片到 FastAPI，服务端保存文件和元数据；桌面端使用 PersonViT 生成 embedding，再调用 embedding 接口保存。查询 gallery 时按模型版本和预处理版本过滤；删除样本会级联删除 embedding 和文件。

## 错误处理

- YOLO26x 缺失或初始化失败：状态栏提示，视频播放不停止。
- PersonViT 缺失：继续显示 YOLO26x 检测框，身份状态为 unknown。
- ReID 输出维度不一致：跳过该 embedding，不让视频线程崩溃。
- 没有新结果超过 TTL：清空实时检测覆盖层。

## 历史兼容

旧姿态字段、旧动作评分和旧姿态复盘入口保留读取兼容，但新训练不会生成这些结果。
