# 视频与实时 AI 模块

[模块地图](README.md) · [实时训练流程](../flows/realtime-training.md) · [第三方依赖与模型](../references/third-party-services.md)

## 职责

- RTSP 和本地文件打开、D3D11VA 解码、断流重连、seek/倍率/逐帧。
- D3D11 frame 渲染与 AI 所需 RGB 提取。
- YOLO person 检测、按机位 ROI、单机位 track、按需 PersonViT ReID。
- 当前 session gallery 与人工绑定。
- 有效身份 + 四点标定下的二维轨迹和速度。

## 视频链路

关键文件：

- `videoopenglwidget.*`：UI 入口、主码流 fallback、文件播放控制。
- `streamregistry.*`：按 URL 复用实时流。
- `rtspstream.*`：FFmpeg 打开/读取、D3D11VA、UDP/TCP、重连、本地 seek。
- `d3d11videodevice.*`：共享 D3D11/FFmpeg hw device。
- `d3dframe.*`、`d3dvideosurface.*`、`d3dframeextractor.*`：帧、显示、RGB 提取。
- `offlinevideoprobe.*`：导入前文件/视频轨/时长/seek/D3D11VA 首帧探测。
- `nvrplayback.*`：按模板生成历史 RTSP 回放 URL。

行为：

- RTSP 优先 UDP，失败后尝试 TCP。
- 断流按 1s/2s/5s/5s 退避；超过 30 秒显示长时间断流。
- 解码输出必须是 D3D11 硬件帧；无通用软件 fallback。
- 本地文件完整支持 seek/倍率/逐帧；普通 RTSP 不保证这些能力。
- 同 URL 实时流可共享；seek 回放使用独立流。

## 实时 AI 链路

关键文件：

- `tensorrtrunner.*`：ONNX 解析、FP16 engine 缓存、CUDA buffer、推理。
- `tensortrtathletebackend.*`：YOLO/PersonViT 前后处理、gallery 匹配、人工绑定。
- `athletedetectionroi.*`：加载并缩放 ROI。
- `athletetracker.*`：同帧一对一 IoU 关联、TTL 和 ReID 节流状态。
- `athleteanalysismanager.*`：多流 round-robin、结果 TTL、线程回调。
- `models/athlete/athlete_models.json`：shape、阈值、版本权威清单。

处理顺序：

```text
D3D frame → RGB → YOLO person → camera ROI → per-camera track
          → 满足策略时 PersonViT → gallery 匹配/人工绑定 → AthleteFrameResult
```

当前默认契约：

- YOLO：`1x3x640x640` → `1x300x6`，只接收 class 0，阈值 0.35。
- PersonViT：`1x3x256x128` RGB，mean/std 0.5，输出 768 维 L2 embedding。
- ReID 阈值 0.60，候选差值 0.05。
- track TTL 1200ms，结果 TTL 350ms，IoU 阈值 0.20。
- track 至少稳定 2 次后再 ReID；unknown 最多 3 次尝试，间隔 1000ms。

若 manifest 与代码不同，先判断是否是代码 bug，再同步二者与测试，不能只改文档。

## ROI、track、身份和轨迹的区别

- ROI：过滤场外检测框；当前配置覆盖 CAM 01–07，缺失/无效时 fail-open。
- track：单机位 bbox 时间关联；不同机位 ID 可重复。
- ReID：把 track 与 `athleteId` 关联；只有当前 gallery 候选可被识别。
- 人工绑定：覆盖 unknown 或低置信度身份，不改变 track 的机位范围。
- 四点标定：把 bbox 底边中点投影到场地米制 `(x,y)`；与 ROI 独立。

## 轨迹和速度

- 只有 identified 且机位四点标定有效时生成。
- 轨迹约每 200ms 采样，权威存储为 `track_points`。
- 速度使用相邻位置和时间差，保存瞬时/1000ms 平滑结果到 `speed_metrics`。
- 首点、时间异常、间隔超过 2 秒或无效检测应标为无效速度，而非伪造 0 速度。
- 当前存在 participant UUID 持久化缺口，见 [已知风险](../05-pitfalls.md)。

## 降级与失败

- YOLO 缺失/失败：视频继续，AI 不可用。
- PersonViT 缺失/失败：保留 person bbox 和 track，身份为 unknown。
- ROI 缺失：不过滤，并应只记录必要告警。
- 结果超过 TTL：清除陈旧叠加。
- 模型更新：旧 embedding 因版本/预处理不匹配不会进入当前 gallery。

## 修改检查

- 视频改动：验证 RTSP UDP/TCP、断流恢复、本地 seek、stop 和密码脱敏。
- 推理改动：验证 shape、坐标缩放、阈值、动态结果数、gallery 版本和 engine 缓存。
- track/ReID 改动：至少覆盖同帧一对一、TTL、gallery 切换、人工绑定和 unknown 重试。
- 轨迹改动：验证标定有效/无效、participant 外键、时间异常和单位。
