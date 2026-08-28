# 排障 Runbook

[Runbook 地图](README.md) · [已知风险](../05-pitfalls.md) · [系统架构](../02-architecture.md)

## 先确定故障层

```text
启动/构建 → FastAPI/PostgreSQL → 视频打开/解码 → RGB 提取/模型
          → ROI/track/ReID → 轨迹/保存 → 完整分析 worker/NAS
```

不要把“画面正常”直接等同“AI 正常”，也不要把“API 200”直接等同“指标已入库”。

## 1. 客户端无法启动/构建

检查顺序：

1. qmake 是否为 `C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe`。
2. 是否在 MSVC x64 Native Tools 环境。
3. `FFMPEG_ROOT`、`TENSORRT_ROOT`、`CUDA_ROOT` 的 header/import lib。
4. 输出目录 Qt platform 插件和运行时 DLL。
5. `iskating.exe` 是否仍运行并锁住 DLL。

不要修改 qmake 生成的 Makefile 作为修复。

## 2. 训练服务/认证失败

1. `GET /health` 是否成功；失败先查数据库 URL、网络和 schema。
2. 服务端 startup 是否在 seed 阶段失败。
3. 登录账号是否 active，默认密码是否已变化。
4. 客户端 `server/baseUrl`、环境覆盖和保存 token 是否一致。
5. JWT 与 RTSP 凭据是两套认证，不要混查。
6. worker 401 检查 `X-Analysis-Worker-Token`；503 表示服务端未配置 token。

日志或工单中不要粘贴 token/密码。

## 3. 视频不播放

检查 UI 状态和 `[RtspStream]` 日志：

- `open attempt`：打开尝试。
- `udp failed, retry tcp`：正在 fallback。
- `stream interrupted` / `reconnect scheduled`：断流与下次重连。
- `long outage`：超过 30 秒。
- `stream recovered`：恢复。

继续检查：IP/端口/路径/凭据、编码、D3D11VA、GPU 驱动。URL 日志必须已脱敏。

本地文件先看 `OfflineVideoProbe` 的文件、视频轨、时长、seek 和首帧硬解错误。当前没有软件解码 fallback。

## 4. 有画面但无 AI

1. 是否已点击开始采集；单视频导入完成不会自动开始 AI。
2. 当前活动流是否进入 `syncAnalysisStreams()`，`trajectoryEnabled` 是否意外关闭分析。
3. 顶部 AI 状态是否从“已就绪”变为等待 frame/运行中。
4. YOLO ONNX、SHA256、TensorRT/CUDA 和 `.engine` 是否匹配。
5. 删除不兼容的本机 `.fp16.engine` 后重建，不提交 engine。
6. `D3DFrameExtractor` 是否得到新 frame。

PersonViT 失败但 YOLO 正常时，应看到 unknown bbox；若连 bbox 都没有，先查 YOLO/活动流，不查 gallery。

## 5. 检测或身份异常

- 检测框缺失：阈值、坐标缩放、ROI polygon/分辨率、class 0。
- 场外人未过滤：该 camera 是否有有效 ROI；CAM 08–12 默认可能 fail-open。
- track 跳变/复用：同帧一对一、IoU、TTL、frame timestamp。
- 身份全 unknown：当前 session gallery、模型/预处理版本、embedding 维度、阈值和重试条件。
- 身份沿用错误：session/gallery 切换是否清空 tracker。
- 跨机位 track 冲突：trackId 本来就不是全局 ID，应看 athleteId。

## 6. 轨迹/速度未保存

1. 实时结果是否 identified。
2. 四点标定是否有效；不要用 ROI 或场地段字段代替。
3. 内存是否形成 track point/speed。
4. POST payload 中 participantId 是否存在。
5. 查询服务端实际生成的 `training_session_participants.id`。
6. 查询 `track_points`/`speed_metrics`，核对 participant 外键。

当前已知客户端/服务端 participant UUID 可能不一致，API 成功仍可静默漏存。

## 7. 历史/回放异常

- `planned` RTSP 资产没有真实文件是当前语义，不是一定的数据丢失。
- `external` 本地文件被移动/替换后需恢复或重新导入。
- RTSP/NVR 不保证精确 seek；本地文件才支持完整控制。
- 新 session 没有动作/评分时，旧复盘和趋势 UI 应明确无数据，不应造分。

## 8. 完整分析异常

1. batch 是否恰好 12 路，URI 是否为 `nas://`。
2. Windows、API、worker 的 NAS 逻辑根是否一致。
3. worker 是否 claim 到 run，lease/heartbeat 是否持续。
4. 容器 GPU、模型挂载、原生 binary 和 gallery TSV。
5. 源 PTS 是否缺失/倒退。
6. chunk 是否已原子完成、SHA256 一致并登记。
7. 数据库完成但回放缺帧时，检查全局 frameIndex/PTS/内容一致性；现有激活检查不覆盖全部。
8. 任务中心 paused 不代表 worker 暂停。

## 日志现状

客户端主要使用 `qDebug()`/`qWarning()`，仓库没有统一日志文件或监控平台。排障记录应包含时间、版本、机位/任务 ID、错误阶段和脱敏错误，不包含密钥与完整 RTSP URL。
