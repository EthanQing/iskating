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
- `athleteanalysismanager.h/cpp`：后台 worker、多路流 round-robin、结果 TTL 和主线程回调。
- `athleteanalysisresult.h`：`AthleteFrameResult`、`AthleteInstance`、gallery 和人工绑定结构。
- `models/athlete/athlete_models.json`：模型来源、shape、阈值、版本和运行时要求。
- `tools/download_athlete_models.ps1`：下载、YOLO 导出和 PersonViT 转换。
- `tools/convert_personvit_msmt17.py`：TransReID checkpoint 到 ONNX 的转换。
- `tools/check_athlete_models.py`：模型存在性和 SHA256 校验。

## 当前设计

- YOLO26x 输入 `1x3x640x640`，端到端输出 `1x300x6`，解析为 `x1,y1,x2,y2,score,classId`，只接受 class 0。
- PersonViT 输入 `1x3x256x128`，RGB，`(pixel - 0.5) / 0.5`，输出 `1x768` 并再次 L2 归一化。
- gallery 只加载当前 session 参与者且模型版本、预处理版本匹配的 embedding。
- 默认阈值为检测 `0.35`、ReID `0.60`、ambiguous margin `0.05`、结果 TTL `350ms`、track TTL `1200ms`，均从 `athlete_models.json` 读取。
- 使用框 IoU 维护同一机位的 track；不同机位不共享 trackId，但共享 athleteId。
- 缺少 PersonViT 时保留 YOLO26x 检测和视频播放，身份状态为 unknown。
- 缺少 YOLO26x 时停止 AI 推理，不影响视频播放。

## 模型与运行时

- TensorRT 10.x、CUDA 11.8、FP16 engine、兼容的 NVIDIA 驱动和 GPU。
- `.fp16.engine` 是机器和 GPU 相关的缓存，不提交 Git。
- 模型二进制默认忽略，发布规则只复制 `models/athlete` 清单和校验文件；部署时需另行准备模型文件。

## 历史兼容

`PoseFrameResult`、旧姿态后端源文件、历史姿态字段和复盘读取代码仍可存在于仓库或数据库中，但不再进入新实时主流程和 Release 模型复制规则。
