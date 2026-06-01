# 视频流模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[frontend|Qt Widgets 前端]]、[[background-workers|后台线程]]、[[ai-inference|AI 推理]]
相关流程：[[flows/video-streaming-flow|视频播放流程]]、[[flows/pose-analysis-flow|姿态分析流程]]
排查入口：[[runbooks/debugging|调试 Runbook]]、[[references/external-apis|外部 API]]、[[05-pitfalls|坑点]]

## 作用

负责 RTSP 或本地视频源接入、低延迟 FFmpeg 解码、D3D11VA 硬件帧管理和 Qt 界面渲染。

## 关键文件

- `videoopenglwidget.h`: 视频控件对外接口，包括本地回放 seek、倍率、逐帧和位置查询。
- `videoopenglwidget.cpp`: 视频源配置、播放/暂停/停止、主码流 fallback、本地回放控制、占位绘制。
- `streamregistry.h`: 按 URL 复用视频流。
- `streamregistry.cpp`: 创建并缓存 `RtspStream`。
- `rtspstream.h`: 视频流状态、帧读取和本地回放控制接口。
- `rtspstream.cpp`: FFmpeg 打开输入、D3D11VA 解码、UDP/TCP RTSP 重试和重连；本地文件按 PTS 控速并支持 seek、倍率和单帧步进。
- `d3d11videodevice.cpp`: 全局 D3D11 设备和 FFmpeg hw device。
- `d3dframe.h`: D3D11 硬件帧封装。
- `d3dvideosurface.cpp`: D3D11 swap chain、shader、视频渲染和骨架叠加。
- `d3dframeextractor.cpp`: 将 D3D 帧复制/转换为 RGB，供 AI 推理使用。

## 当前设计

- `VideoOpenGLWidget` 是 UI 层入口。
- `StreamRegistry::acquire(url)` 按 URL 返回共享 `RtspStream`。
- `RtspStream` 后台线程使用 FFmpeg 打开视频源，优先 RTSP UDP，失败后尝试 TCP。
- 离线视频导入复用同一条链路：`MainWindow::importOfflineVideo()` 选择本地文件后，主视图通过 `VideoOpenGLWidget::playFile()` 打开文件并把 active stream 交给 AI 分析。
- 解码必须输出 `AV_PIX_FMT_D3D11`，否则视为 fatal error。
- `D3DVideoSurface` 负责把最新 `D3DFrame` 显示到 Qt 控件。
- `HandAnalysisManager` 用 `D3DFrameExtractor` 从当前分析流转 RGB；采集中会由 `MainWindow::syncAnalysisStreams()` 把参与轨迹的 12 路相机活动流同步给 AI。
- RTSP 训练时，选中机位优先使用主视图主码流，其他参与轨迹机位使用小窗预览流；离线视频仍只分析主视图单路本地文件。
- 本地文件回放使用独立 `RtspStream`，不经过 `StreamRegistry` 共享；RTSP/网络源继续走共享低延迟流。
- `D3DFrame::mediaTimeMs` 保存媒体时间戳，供 UI 查询当前位置、片段定位和逐帧回放使用。

## 对外接口

其他模块主要通过 `VideoOpenGLWidget` 使用视频能力：

- `setStreamUrls(previewUrl, mainUrl)`
- `playDefaultVideo()`
- `playMainUrlWithFallback(mainUrl, fallbackUrl)`
- `playFile(filePath)`
- `playFile(filePath, startPositionMs)`
- `seekTo(positionMs)`
- `setPlaybackRate(rate)`
- `stepForward()`
- `positionMs()`
- `durationMs()`
- `isSeekable()`
- `pausePlayback()`
- `stopPlayback()`
- `activeStream()`
- `setPoseFrame(frame)`

## 常见修改任务

### 调整 RTSP 连接参数

1. 阅读 `setRtspOptions()` in `rtspstream.cpp`。
2. 确认修改是否影响低延迟、重连和 UDP/TCP fallback。
3. 使用真实摄像头验证连接、断流和恢复。

### 修改主码流 fallback

1. 阅读 `VideoOpenGLWidget::refreshVideoFrame()`。
2. 确认 fallback 条件是否只针对 D3D11/硬解相关错误。
3. 验证主码流不可用时是否正确回退预览码流。

### 新增视频状态提示

1. 修改 `RtspStream::setState()` 或 `VideoOpenGLWidget::refreshVideoFrame()`。
2. 保持状态文本中文且简洁。
3. 避免日志输出明文密码。

### 调整离线视频导入

1. 入口在主视频标题栏的“导入视频”按钮，逻辑集中在 `MainWindow::importOfflineVideo()` 和 `showOfflineVideoInMainView()`。
2. 选中离线视频后 `m_selectedCamera` 为 0，开始采集不会切回 CAM 01，也不会启动 12 路 RTSP 预览。
3. AI 分析只订阅主视图本地文件流，不会走 12 路相机轨迹拼接。
4. 保存训练记录时 `video_source` 写入本地文件绝对路径，`video_camera_name` 写入“离线视频 · 文件名”。

### 调整本地复盘回放

1. 本地文件的 seek、慢放和逐帧能力在 `RtspStream` 内实现，UI 只通过 `VideoOpenGLWidget` 调用。
2. 新增控制前先判断 `isSeekable()`，避免把 RTSP 当作可随机访问媒体。
3. 片段定位应使用动作的有效起止时间或 `video_clip_start_ms/end_ms`，旧记录缺失时回退到动作开始时间。

## 注意事项

- ⚠️ 高风险区域：当前没有通用软件解码 fallback。
- 离线视频仍要求解码器支持 D3D11VA；不兼容编码会像 RTSP 一样进入视频错误状态。
- 离线训练记录只保存本地文件引用，不复制视频文件；后续回看依赖原文件仍在本机可访问。
- 历史复盘的精确 seek、慢放和逐帧仅对本地离线视频可用；RTSP/网络视频按实时源打开并提示片段时间，保持低延迟播放路径不变。
- ⚠️ 高风险区域：D3D11 设备是全局共享的，修改线程/生命周期要谨慎。
- 不要在日志中直接打印未脱敏 RTSP URL。
- `RtspStream::stop()` 等待 8 秒后会 terminate 线程，这是最后手段，修改时要考虑 FFmpeg 阻塞。

## 相关流程

- `../flows/video-streaming-flow.md`
- `../flows/pose-analysis-flow.md`

## 未确认问题

- TODO: 未确认目标摄像头品牌、编码格式和码流路径约定。
- TODO: 未确认是否需要软件解码 fallback。
