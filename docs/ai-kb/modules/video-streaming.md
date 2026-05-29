# 视频流模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[frontend|Qt Widgets 前端]]、[[background-workers|后台线程]]、[[ai-inference|AI 推理]]
相关流程：[[flows/video-streaming-flow|视频播放流程]]、[[flows/pose-analysis-flow|姿态分析流程]]
排查入口：[[runbooks/debugging|调试 Runbook]]、[[references/external-apis|外部 API]]、[[05-pitfalls|坑点]]

## 作用

负责 RTSP 或本地视频源接入、低延迟 FFmpeg 解码、D3D11VA 硬件帧管理和 Qt 界面渲染。

## 关键文件

- `videoopenglwidget.h`: 视频控件对外接口。
- `videoopenglwidget.cpp`: 视频源配置、播放/暂停/停止、主码流 fallback、占位绘制。
- `streamregistry.h`: 按 URL 复用视频流。
- `streamregistry.cpp`: 创建并缓存 `RtspStream`。
- `rtspstream.h`: 视频流状态和帧读取接口。
- `rtspstream.cpp`: FFmpeg 打开输入、D3D11VA 解码、UDP/TCP RTSP 重试和重连。
- `d3d11videodevice.cpp`: 全局 D3D11 设备和 FFmpeg hw device。
- `d3dframe.h`: D3D11 硬件帧封装。
- `d3dvideosurface.cpp`: D3D11 swap chain、shader、视频渲染和骨架叠加。
- `d3dframeextractor.cpp`: 将 D3D 帧复制/转换为 RGB，供 AI 推理使用。

## 当前设计

- `VideoOpenGLWidget` 是 UI 层入口。
- `StreamRegistry::acquire(url)` 按 URL 返回共享 `RtspStream`。
- `RtspStream` 后台线程使用 FFmpeg 打开视频源，优先 RTSP UDP，失败后尝试 TCP。
- 解码必须输出 `AV_PIX_FMT_D3D11`，否则视为 fatal error。
- `D3DVideoSurface` 负责把最新 `D3DFrame` 显示到 Qt 控件。
- `HandAnalysisManager` 用 `D3DFrameExtractor` 从活动主视图帧转 RGB。

## 对外接口

其他模块主要通过 `VideoOpenGLWidget` 使用视频能力：

- `setStreamUrls(previewUrl, mainUrl)`
- `playDefaultVideo()`
- `playMainUrlWithFallback(mainUrl, fallbackUrl)`
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

## 注意事项

- ⚠️ 高风险区域：当前没有通用软件解码 fallback。
- ⚠️ 高风险区域：D3D11 设备是全局共享的，修改线程/生命周期要谨慎。
- 不要在日志中直接打印未脱敏 RTSP URL。
- `RtspStream::stop()` 等待 8 秒后会 terminate 线程，这是最后手段，修改时要考虑 FFmpeg 阻塞。

## 相关流程

- `../flows/video-streaming-flow.md`
- `../flows/pose-analysis-flow.md`

## 未确认问题

- TODO: 未确认目标摄像头品牌、编码格式和码流路径约定。
- TODO: 未确认是否需要软件解码 fallback。
