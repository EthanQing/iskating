# 视频播放流程

上级入口：[[00-index|AI 知识库索引]]、[[flows/README|流程地图]]
相关模块：[[modules/video-streaming|视频流]]、[[modules/background-workers|后台线程]]、[[modules/frontend|Qt Widgets 前端]]
相邻流程：[[main-user-flow|主用户流程]]、[[pose-analysis-flow|姿态分析流程]]
排查入口：[[runbooks/debugging|调试 Runbook]]、[[references/external-apis|外部 API]]、[[05-pitfalls|坑点]]

## 简介

把 RTSP 或本地视频源连接到 Qt 视频控件，使用 FFmpeg+D3D11VA 解码并通过 D3D surface 显示。

## 触发条件

- 用户点击开始采集。
- 用户在主视频标题栏点击“导入视频”并选择本地文件。
- 用户双击某个摄像头小窗切换主视图。
- `applyCameraSettingsToWidgets(true)` 恢复播放。

## 流程步骤

1. `MainWindow` 调用 `VideoOpenGLWidget::setStreamUrls()` 设置预览和主码流。
2. 小窗调用 `playDefaultVideo()` 接入预览码流。
3. 主视图调用 `playMainUrlWithFallback()` 接入主码流，并保存预览码流作为 fallback。
4. `VideoOpenGLWidget::attachStream()` 通过 `StreamRegistry::acquire()` 获取共享 `RtspStream`。
5. `RtspStream` 后台线程用 FFmpeg 打开源，RTSP 先 UDP，失败后 TCP。
6. 解码器必须支持 D3D11VA，成功后输出 `D3DFrame`。
7. `VideoOpenGLWidget::refreshVideoFrame()` 取最新帧并交给 `D3DVideoSurface::presentFrame()`。

离线视频路径：

1. `MainWindow::importOfflineVideo()` 用 `QFileDialog` 选择本地视频文件，并保存上次目录到 `QSettings/offlineVideo/lastDir`。
2. `showOfflineVideoInMainView()` 将主视图来源切到“离线视频 · 文件名”，调用 `VideoOpenGLWidget::playFile()`。
3. `m_selectedCamera` 设为 0，摄像头小窗取消选中；开始采集时保持离线视频为主分析源。
4. 离线模式下不会启动 12 路 RTSP 预览，避免离线复盘时额外占用解码资源。

## 涉及文件

- `mainwindow.cpp`
- `videoopenglwidget.cpp`
- `streamregistry.cpp`
- `rtspstream.cpp`
- `d3d11videodevice.cpp`
- `d3dframe.h`
- `d3dvideosurface.cpp`

## 涉及数据

- RTSP URL
- `RtspStream::State`
- `D3DFrame`
- `PoseFrameResult`，用于视频叠加骨架

## 错误处理

- FFmpeg 打不开源时设置 `RtspStream::State::Error` 并进入重连。
- 解码器不支持 D3D11VA 时设置 fatal error。
- 主码流遇到 D3D11/硬解相关错误时，主视图尝试回退预览码流。
- 本地文件不存在时视频控件显示“文件不存在”。
- 离线视频文件被移动或删除后，开始采集会提示重新导入。

## 边界情况

- 同一 URL 被多个控件使用时，`StreamRegistry` 会复用同一个 `RtspStream`。
- 暂停播放会释放控件持有的流引用，不是向 `RtspStream` 发送 pause。
- 没有控件引用后，共享流会随 `shared_ptr` 生命周期结束。
- 离线视频暂停后再次开始会重新打开文件，当前 v1 不保存播放进度或自动 seek。
