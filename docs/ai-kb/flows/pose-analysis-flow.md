# 姿态分析流程

上级入口：[[00-index|AI 知识库索引]]、[[flows/README|流程地图]]
相关模块：[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/pose-analysis|姿态分析]]、[[modules/background-workers|后台线程]]
相邻流程：[[video-streaming-flow|视频播放流程]]、[[training-record-flow|训练记录流程]]
排查入口：[[runbooks/debugging|调试 Runbook]]、[[references/third-party-services|第三方服务]]、[[05-pitfalls|坑点]]

## 简介

把采集中参与轨迹的相机活动流转换成 RGB 图像，输入 TensorRT 姿态模型，再把结果按 `cameraId` 回写到 UI、轨迹和评分模块。离线视频仍是主视图单路分析。

## 触发条件

- 主视图或相机小窗开始播放并通知 `setStreamChangedHandler()`。
- 用户点击开始采集、暂停/恢复、切换摄像头或修改系统设置。
- `HandAnalysisWorker` 未暂停并检测到新帧。

## 流程步骤

1. `MainWindow::syncAnalysisStreams()` 收集参与轨迹相机的活动流，并调用 `HandAnalysisManager::setActiveStreams()`。
2. `HandAnalysisWorker` 启动时加载 `models/body` 下的人体模型。
3. worker 在多路 `RtspStream::latestFrame()` 之间 round-robin 选择有新帧的流。
4. `D3DFrameExtractor::copyToRgb()` 将 D3D 帧转换成 `QImage`。
5. `TensorRtBodyPoseBackend::infer()` 先运行 YOLOv8n-pose。
6. 如果 RTMW3D 已就绪，继续补充 3D 关键点。
7. worker 用 queued callback 发布带 `cameraId` 的 `PoseFrameResult`。
8. `MainWindow` 只用选中机位结果更新主视频覆盖层和骨架视图；所有机位结果都会进入轨迹视图。
9. `TrajectoryWidget` 根据系统设置中的相机覆盖段，把图像锚点线性映射到场地坐标并绘制全场轨迹。
10. 动作计数和评分继续复用现有实时评分链路。

## 涉及文件

- `handanalysismanager.cpp`
- `d3dframeextractor.cpp`
- `tensorrtbodyposebackend.cpp`
- `tensorrtrtmw3dbackend.cpp`
- `tensorrtrunner.cpp`
- `mainwindow.cpp`
- `posestandardnessscorer.cpp`

## 涉及数据

- `D3DFrame`
- `QImage`
- `TensorRtOutput`
- `PoseFrameResult`
- `PoseStandardnessResult`

## 错误处理

- 模型初始化失败时，状态栏显示“人体姿态 AI 初始化失败”。
- 取帧失败时发布“人体姿态 AI 取帧失败”。
- 推理失败时后端返回空结果并更新状态文本。
- 超过结果 TTL 无新结果时发布空姿态，清除显示。

## 边界情况

- 没有活动分析流时不推理。
- 当前全场轨迹是按相机覆盖段做线性拼接，不是基于棋盘格/AprilTag/内外参的单应性标定，也不是多相机三维三角化。
- `rtmw3d-x.onnx` 缺失时，2D 人体姿态仍可运行。
- `HandAnalysisManager` 名称含 Hand，但当前主流程实际是人体姿态分析。
