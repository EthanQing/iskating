# 后台线程模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[video-streaming|视频流]]、[[ai-inference|AI 推理]]、[[core|应用核心]]
相关流程：[[flows/video-streaming-flow|视频播放流程]]、[[flows/pose-analysis-flow|运动员检测与身份流程]]
排查入口：[[runbooks/debugging|调试 Runbook]]、[[05-pitfalls|坑点]]

## 作用

将视频读取和 AI 推理从 UI 线程移出，避免界面卡顿，同时通过 Qt queued callback 安全回到主线程更新 UI。

## 关键文件

- `rtspstream.cpp`: 每个视频源使用 `QThread::create()` 后台读取和解码。
- `athleteanalysismanager.cpp`: `AthleteAnalysisWorker` 后台初始化 TensorRT 并循环分析最新帧。
- `streamregistry.cpp`: 管理共享 `RtspStream` 生命周期。
- `mainwindow.cpp`: 创建 `AthleteAnalysisManager` 并设置回调。

## 当前设计

- `RtspStream::start()` 创建线程，循环 `openAndDecodeOnce()`，断流后重连。
- `RtspStream::stop()` 设置 stop flag，最多等待 8 秒后 terminate。
- `AthleteAnalysisWorker::run()` 初始化模型，然后按分析档位轮询最新帧：`fast` 约 100ms，`balanced` 约 66ms，`high` 约 33ms。
- `AthleteAnalysisManager::setActiveStreams()` 支持多路 `RtspStream`，worker 按轮询顺序在有新帧且达到目标 FPS 间隔的流之间 round-robin 分析，并把 `cameraId` 写回 `AthleteFrameResult`。
- AI 分析结果通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 发送回 `MainWindow`。
- `AthleteAnalysisWorker::stop()` 等待最长 180 秒，因为 TensorRT 关闭可能很慢。

## 对外接口

- `AthleteAnalysisManager::setActiveStreams(streams)`：采集中用于同步多路相机分析流。
- `AthleteAnalysisManager::setAnalysisProfile(profile)`：设置 `fast` / `balanced` / `high` 轮询间隔。
- `AthleteAnalysisManager::setPaused(paused)`
- `AthleteAnalysisManager::stop()`
- `RtspStream::latestFrame()`
- `RtspStream::statusText()`

## 常见修改任务

### 调整 AI 分析频率

1. 优先检查 `intervalForProfile()` in `athleteanalysismanager.cpp`。
2. 评估 TensorRT 推理耗时和 GPU 占用。
3. 验证 UI 刷新、动作计数和视频播放是否稳定。

### 调整结果过期策略

1. 修改 `kResultTtlMs` in `athleteanalysismanager.cpp`。
2. 确认短暂断帧时检测框是否应该保留。
3. 验证暂停/停止时 `clearRealtimePose()` 行为。

## 注意事项

- 不要从 worker 线程直接操作 QWidget。
- 不要在持有 mutex 时调用可能回调 UI 或耗时的逻辑。
- 多路分析是单个 TensorRT worker 在多路流之间轮询，不是 12 个并行模型实例；12 路同时接入时实际每路 FPS 取决于 GPU、解码、目标 FPS、自动降级和档位。
- `StreamRegistry` 使用 weak pointer；没有控件引用时流会自然释放。
- 修改线程 stop 逻辑要特别小心 FFmpeg 阻塞和 TensorRT 析构耗时。

## 相关流程

- `../flows/video-streaming-flow.md`
- `../flows/pose-analysis-flow.md`

## 未确认问题

- TODO: 未确认目标机器上同时解码 12 路 RTSP 的性能上限。
