# 实时训练流程

[流程地图](README.md) · [视频与实时 AI](../modules/video-and-realtime-ai.md) · [已知风险](../05-pitfalls.md)

## 前置条件

- 至少一路已配置且允许进入分析的 RTSP 相机，或已导入本地视频。
- YOLO 模型可用时才有 AI；PersonViT 可选降级。
- 当前 session 已选择主运动员、动作标准，并可选择最多 3 名附加参与者。
- 轨迹另需运动员 identified 且机位四点标定有效。

## RTSP 视频启动

1. `MainWindow` 从 QSettings 的公共参数和每路 IP 生成预览/主码流 URL。
2. 小窗播放预览；主视图优先主码流并保存预览 fallback。
3. `StreamRegistry` 按 URL 获取共享 `RtspStream`。
4. `RtspStream` 后台先 UDP 后 TCP 打开并输出 D3D11 frame。
5. `VideoOpenGLWidget` 定时显示最新 frame；错误时显示状态和重连信息。

## AI 时序

1. `MainWindow::syncAnalysisStreams()` 根据当前机位、最大路数、目标 FPS 和自动降级生成活动流。
2. 单个 `AthleteAnalysisWorker` 在流之间 round-robin 选择达到间隔且有新 frame 的流。
3. `D3DFrameExtractor` 转为 RGB。
4. YOLO 输出 person bbox；按 camera ROI 过滤。
5. tracker 在该机位完成同帧一对一关联并更新 track 状态。
6. 满足稳定次数、框置信度/面积和重试间隔时运行 PersonViT。
7. embedding 与当前 session gallery 比较；超过阈值且不歧义时得到 athleteId，否则 unknown。
8. 人工绑定可覆盖该 camera/track 的 unknown 或低置信度结果。
9. queued callback 回 UI，更新 bbox、身份标签和 trackId。
10. 约每 200ms 形成检测/身份兼容摘要；identified + 四点标定时形成轨迹点和速度结果。

## 采集状态

- **开始**：启动计时、同步活动流并接受结果。
- **暂停**：暂停 AI/训练计时并清理当前实时叠加；已积累内存数据仍保留。
- **停止**：停止本次采集，等待用户保存。
- **保存**：进入 [Session 保存流程](session-and-review.md)。

## 失败与降级

- 主码流 D3D11/硬解失败时，主视图可尝试预览 fallback。
- RTSP 断流进入重连；不应把旧 frame/检测结果无限保留。
- YOLO 初始化失败：视频继续、AI 状态报错。
- PersonViT 初始化失败：bbox/track 继续，身份 unknown。
- ROI 缺失：fail-open；不能显示成“已应用冰面限制”。
- 标定无效：不生成场地点，不能回退为像素轨迹。
- gallery 变化：清空旧 track，避免身份沿用到新 session。

## 验收重点

- UI 线程无视频/推理阻塞。
- 多人同帧不会复用同一个旧 track。
- 不同相机可出现相同 trackId，但 athleteId 语义不混淆。
- 暂停、停止、断流和结果 TTL 后不残留陈旧框。
- 未标定或 unknown 不产生轨迹；有效条件下单位和时间戳正确。
