# 术语表

[返回索引](00-index.md)

| 术语 | 含义 |
|---|---|
| session | 一次保存的训练记录，权威表为 `training_sessions`。完整分析 run 完成不会自动生成 session。 |
| participant | session 内参与运动员，最多 4 名；主运动员也是 participant。 |
| athleteId | 运动员档案 UUID；跨机位识别同一人依赖它。 |
| trackId | 单机位、单次跟踪上下文中的临时轨迹 ID，不全局唯一。 |
| gallery | 当前允许参与身份匹配的 ReID embedding 集合；Windows 实时通常限当前 session 参与者。 |
| YOLO26x | 当前 person 检测器，输出 COCO class 0 人体框。 |
| PersonViT / TransReID | 当前 ReID 模型，输出 768 维归一化 embedding。 |
| ROI | 检测区域多边形；以 bbox 底边中点判定是否保留检测，不等同场地标定。 |
| 四点标定 / homography | 四组像素点与场地米制点建立的单应性，用于二维轨迹投影。 |
| D3D11VA | Windows FFmpeg 硬件解码路径；当前没有通用软件解码 fallback。 |
| D3DFrame | 客户端共享的 D3D11 硬件帧封装，含媒体时间戳。 |
| 实时分析 | Windows 客户端从活动流 latest frame 抽样执行的低延迟 AI。 |
| 单视频导入 | Windows 对一个本地文件完成探测/登记，再按播放进度执行实时 AI。 |
| 完整帧率分析 | Ubuntu DeepStream 对 12 路 NAS 视频的每个解码帧执行分析。 |
| batch | 一组恰好 12 路完整分析源及其同步信息。 |
| run | batch 的一次分析版本，含状态、源进度和模型/预处理标签。 |
| run source | 某个 run 中的一路机位执行状态。 |
| chunk | 完整分析约 10 秒一个的 gzip JSONL 结果分块。 |
| activate | 将已完成 run 设为 batch 当前结果版本；不会自动创建 session。 |
| `nas://` | 跨 Windows/Linux 保存的逻辑 NAS URI；各主机映射到自己的物理根目录。 |
| `participant_pose_frames` | 历史兼容时间线；新数据主要承载 bbox/track/身份摘要，不是权威轨迹。 |
| `track_points` | 四点标定后二维米制轨迹的权威表。 |
| `speed_metrics` | 与轨迹点一对一关联的瞬时/平滑速度结果。 |
| `joint_metrics` | 关节角与角速度表；当前实时/完整 AI 不生成新数据。 |
| planned video | 已登记规范路径但未实际录制的 RTSP 视频资产。 |
| QSettings | Windows 本机配置存储；不保存新的训练业务主数据。 |
| TrainingRepository | Qt 客户端到 FastAPI 的数据访问边界。 |
| soft archive | 通过 `active=false` 隐藏实体，同时保留历史外键。 |
| effective value | 复盘中人工修正优先、无人工修正时回退 AI 原始值的展示/汇总值。 |
