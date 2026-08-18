# AI 推理模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[video-streaming|视频流]]、[[background-workers|后台线程]]
相关流程：[[flows/pose-analysis-flow|运动员检测与身份流程]]
相关 Reference：[[references/third-party-services|第三方服务]]、[[references/environment-variables|环境变量]]、[[05-pitfalls|坑点]]

## 作用

负责使用 TensorRT 加载 YOLO26x 和 PersonViT，为视频帧输出运动员检测框、ReID embedding、身份匹配和跨帧 trackId。

## 关键文件

- `tensorrtrunner.h/cpp`：ONNX 解析、FP16 engine 构建/缓存、CUDA buffer 和推理。
- `tensortrtathletebackend.h/cpp`：YOLO26x 解码、PersonViT crop、余弦匹配、人工绑定和 per-camera track。
- `athletedetectionroi.h/cpp`：加载、缩放并验证每路检测 ROI。
- `athletetracker.h/cpp`：同机位一对一 IoU 关联、track TTL、命中次数与 ReID 节流状态。
- `athleteanalysismanager.h/cpp`：后台 worker、多路流 round-robin、结果 TTL 和主线程回调。
- `athleteanalysisresult.h`：`AthleteFrameResult`、`AthleteInstance`、gallery 和人工绑定结构。
- `models/athlete/athlete_models.json`：模型来源、shape、阈值、版本和运行时要求。
- `models/athlete/camera_detect_rois.json`：CAM 01–07 的原始画面 ROI 多边形。
- `tools/download_athlete_models.ps1`：下载、YOLO 导出和 PersonViT 转换。
- `tools/convert_personvit_msmt17.py`：TransReID checkpoint 到 ONNX 的转换。
- `tools/check_athlete_models.py`：模型存在性和 SHA256 校验。

## 当前设计

- YOLO26x 输入 `1x3x640x640`，端到端输出 `1x300x6`，解析为 `x1,y1,x2,y2,score,classId`，只接受 class 0。
- PersonViT 输入 `1x3x256x128`，RGB，`(pixel - 0.5) / 0.5`，输出 `1x768` 并再次 L2 归一化。
- gallery 只加载当前 session 参与者且模型版本、预处理版本匹配的 embedding。
- 默认阈值为检测 `0.35`、ReID `0.60`、ambiguous margin `0.05`、结果 TTL `350ms`、track TTL `1200ms`，均从 `athlete_models.json` 读取。
- 检测后先按 camera ROI 过滤：多边形以原始画面像素保存，随当前帧缩放，并以 bbox 底边中点判定。CAM 08–12 或缺失/无效配置时 fail-open，不丢弃检测，并只记录一次告警。
- 同一机位使用全帧一对一 IoU 关联维护 track，避免同一旧 track 在一帧内被多个检测框复用；不同机位不共享 trackId，但共享 athleteId。
- `trackAssistedReid` 默认启用：unknown track 至少稳定 2 次、检测框满足最低置信度和面积后才运行 PersonViT；失败最多重试 3 次、间隔 1000ms。已识别身份沿 track 传播，gallery 切换会清空旧 track，人工绑定仍只覆盖低置信度或 unknown 结果。
- 缺少 PersonViT 时保留 YOLO26x 检测和视频播放，身份状态为 unknown。
- 缺少 YOLO26x 时停止 AI 推理，不影响视频播放。

## 完整帧率离线执行器

- `analysis_worker/` 是独立 Ubuntu/DeepStream 9 服务，不复用 Windows latest-frame 实时 worker。
- 原生 C++ 管线使用硬件解码、`nvstreammux`、YOLO26x PGIE、NvDCF、PersonViT 张量输出和自定义结果收集器；所有队列禁用 leaky/drop，YOLO `interval=0`。
- `analysis_worker/custom_parser` 解析 YOLO26x `batch x 300 x 6` NMS-free 输出，PGIE 关闭额外聚类。
- `tools/export_deepstream_models.py` 导出 YOLO batch 1-12 和 PersonViT batch 1-32 的动态 ONNX，并生成模型校验元数据。
- PersonViT 只在新 track、固定周期或身份低置信度/冲突时执行，gallery 匹配和 ambiguous margin 保持业务规则；结果沿 track 传播，不保存长期原始 embedding。
- 每个解码帧都生成 `frameIndex`、PTS、批次时间、cameraId 和对象列表。PTS 缺失或倒退会使该源失败，不会静默补帧。
- 当前结果收集器只提交 bbox、单机位 track 和 ReID 身份结果，不生成场地轨迹、速度、关节点、姿态、动作计数或评分。

## 模型与运行时

- TensorRT 10.x、CUDA 11.8、FP16 engine、兼容的 NVIDIA 驱动和 GPU。
- `.fp16.engine` 是机器和 GPU 相关的缓存，不提交 Git。
- 模型二进制默认忽略，发布规则会复制 `models/athlete` 清单、校验文件和 `camera_detect_rois.json`；部署时仍需另行准备模型文件。

## 历史兼容

服务端仍可保留历史姿态字段和数据表，但客户端已删除旧姿态结果类型、后端和复盘关键点覆盖层；这些内容不再进入新实时主流程和 Release 模型复制规则。
