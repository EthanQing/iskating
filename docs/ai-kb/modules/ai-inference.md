# AI 推理模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[video-streaming|视频流]]、[[pose-analysis|姿态分析]]、[[background-workers|后台线程]]
相关流程：[[flows/pose-analysis-flow|姿态分析流程]]
相关 Reference：[[references/third-party-services|第三方服务]]、[[references/external-apis|外部 API]]、[[05-pitfalls|坑点]]

## 作用

负责加载 ONNX/TensorRT 模型，对 RGB 帧进行人体 2D 姿态、RTMW3D 3D 姿态和手部姿态推理。

## 关键文件

- `tensorrtrunner.h`: 通用 TensorRT runner 接口。
- `tensorrtrunner.cpp`: ONNX 解析、FP16 engine 构建/缓存、CUDA buffer 和推理。
- `tensorrtbodyposebackend.cpp`: YOLOv8n-pose 2D Body17 检测与 NMS。
- `tensorrtrtmw3dbackend.cpp`: RTMW3D 3D 关键点补充。
- `tensortrthandposebackend.cpp`: palm detector + hand landmark 手部模型后端。
- `handposeadapter.cpp`: 将手部结果转为 `PoseFrameResult`。
- `models/body/body_model.json`: 人体模型配置说明。
- `models/hand/hand_model.json`: 手部模型配置说明。

## 当前设计

- `TensorRtRunner::initialize()` 优先读取同目录 `.fp16.engine`，没有则从 ONNX 构建。
- 所有 engine 使用 FP16 flag。
- `TensorRtBodyPoseBackend` 是当前主流程使用的后端。
- `TensorRtBodyPoseBackend` 会先跑 YOLOv8n-pose，再尝试调用 `TensorRtRtmw3dBackend`。
- `TensorRtRtmw3dBackend` 缺失时不阻止 2D 人体姿态使用。
- 手部后端存在，但当前未接入 `HandAnalysisManager` 主流程。

## 对外接口

- `TensorRtRunner::initialize(onnxPath, error)`
- `TensorRtRunner::infer(input, outputs, error)`
- `TensorRtBodyPoseBackend::initialize(modelDir, error)`
- `TensorRtBodyPoseBackend::infer(rgbFrame, cameraId, timestampMs)`

## 常见修改任务

### 替换人体 2D 模型

1. 更新 `models/body/body_model.json`。
2. 确认 `tensorrtbodyposebackend.cpp` 的输出解析仍匹配模型 layout。
3. 删除旧本地 `.fp16.engine` 后重新构建验证。

### 调整阈值

1. 阅读 `tensorrtbodyposebackend.cpp` 中的 `kBoxScoreThreshold`, `kKeypointThreshold`, `kNmsIouThreshold`。
2. 结合真实视频验证误检/漏检。
3. 更新知识库中的风险或模型说明。

### 接入手部后端

1. 阅读 `tensortrthandposebackend.cpp` 和 `handposeadapter.cpp`。
2. 设计人体/手部结果合并策略。
3. 修改 `HandAnalysisManager` 时注意线程和回调生命周期。

## 注意事项

- `.fp16.engine` 是生成缓存，不要提交。
- 首次构建 engine 可能耗时数分钟。
- engine 与 TensorRT/CUDA/GPU/模型版本相关，不应跨环境复用。
- `tensorrtrunner.cpp` 硬编码了默认 TensorRT/CUDA DLL 搜索路径。
- 修改输出解析时必须同步 `PoseFrameResult` 的 skeletonType 和 keypoint 语义。

## 相关流程

- `../flows/pose-analysis-flow.md`

## 未确认问题

- TODO: 未确认模型精度选项 `fast/balanced/high` 是否实际影响推理后端；当前主要用于 UI 与记录字段。
- TODO: 未确认手部模型是否仍是规划内能力。
