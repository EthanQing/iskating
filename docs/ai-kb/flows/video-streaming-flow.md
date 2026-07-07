# 视频播放流程

上级入口：[[00-index|AI 知识库索引]]、[[flows/README|流程地图]]
相关模块：[[modules/video-streaming|视频流]]、[[modules/background-workers|后台线程]]、[[modules/frontend|Qt Widgets 前端]]
相邻流程：[[main-user-flow|主用户流程]]、[[pose-analysis-flow|姿态分析流程]]
排查入口：[[runbooks/debugging|调试 Runbook]]、[[references/external-apis|外部 API]]、[[05-pitfalls|坑点]]

## 简介

把 RTSP 或本地视频源连接到 Qt 视频控件，使用 FFmpeg+D3D11VA 解码并通过 D3D surface 显示。

## 触发条件

- 用户在系统设置中点击“连通测试”。
- 用户点击开始采集。
- 用户在主视频标题栏点击“导入视频”并选择本地文件。
- 用户双击某个摄像头小窗切换主视图。
- 用户在训练历史复盘卡点击“回看视频”或“定位片段”。
- `applyCameraSettingsToWidgets(true)` 恢复播放。

配置阶段连通测试：

1. `SystemSettingsDialog::testCameraConnectivity()` 读取当前表单的公共 RTSP 参数和 12 路 IP。
2. `CameraConnectivityTester` 对每路非空 IP 生成预览流 RTSP URL，空 IP 直接标记未配置。
3. 探测按 UDP 打开，失败后切换 TCP；成功后读取视频流信息和首帧。
4. 结果表展示每路成功/失败、协议、分辨率、帧率和错误原因；结果不会保存到 QSettings，也不会影响正在播放的视频控件。

## 流程步骤

1. `MainWindow` 调用 `VideoOpenGLWidget::setStreamUrls()` 设置预览和主码流。
2. 小窗调用 `playDefaultVideo()` 接入预览码流。
3. 主视图调用 `playMainUrlWithFallback()` 接入主码流，并保存预览码流作为 fallback。
4. `VideoOpenGLWidget::attachStream()` 通过 `StreamRegistry::acquire()` 获取共享 `RtspStream`。
5. `RtspStream` 后台线程用 FFmpeg 打开源，RTSP 先 UDP，失败后 TCP，并记录当前 transport。
6. 解码器必须支持 D3D11VA，成功后输出 `D3DFrame`。
7. `VideoOpenGLWidget::refreshVideoFrame()` 取最新帧并交给 `D3DVideoSurface::presentFrame()`。

断流恢复路径：

1. `avformat_open_input()`、`avformat_find_stream_info()`、视频轨道查找或 `av_read_frame()` 失败时，`RtspStream` 记录最近错误和最近断流时间。
2. `run()` 使用 1s/2s/5s/5s 退避重连，并累计连续重连次数和总重连次数。
3. 重连中状态显示“断流重连中，N 秒后重连（第 X 次）”；超过 30 秒未恢复时显示长时间断流提示。
4. 重新进入 `Playing` 后记录最近恢复时间、恢复耗时并清零连续重连次数。

离线视频路径：

1. `MainWindow::importOfflineVideo()` 用 `QFileDialog` 选择本地视频文件，并保存上次目录到 `QSettings/offlineVideo/lastDir`。
2. `OfflineVideoProbe` 校验文件存在、可读、非空、可打开、有视频轨、时长有效、支持 seek、解码器支持 D3D11VA，并能输出首个 D3D11 硬件帧。
3. 校验失败时弹出“导入失败”，展示具体原因，不改变当前播放源。
4. 校验成功后 `showOfflineVideoInMainView()` 将主视图来源切到“离线视频 · 文件名”，调用 `VideoOpenGLWidget::playFile()`。
5. `m_selectedCamera` 设为 0，摄像头小窗取消选中；开始采集时保持离线视频为主分析源。
6. 离线模式下不会启动 12 路 RTSP 预览，避免离线复盘时额外占用解码资源。

历史复盘路径：

1. `MainWindow::openSessionVideo()` 读取 `training_sessions.video_source`，为空时回退 `video_fallback_source`。
2. 本地离线视频会先检查文件是否仍存在；存在时调用 `VideoOpenGLWidget::playFile(filePath, videoClipStartMs)`。
3. `RtspStream` 在打开本地文件、读取视频轨道后用 `av_seek_frame()` 尝试跳到 `video_clip_start_ms`，再进入 D3D11VA 解码。
4. RTSP/网络视频不会执行自动 seek，只打开保存的视频源并在主界面提示片段起点可人工参考。

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

- FFmpeg 打不开源时设置 `RtspStream::State::Error`、记录断流原因并进入重连。
- RTSP UDP 打开失败时记录 fallback 事件，再使用 TCP 尝试打开。
- 断流重连期间 UI 直接显示 `RtspStream::statusText()`，长时间断流会提示检查摄像头网络或 RTSP 配置。
- 解码器不支持 D3D11VA 时设置 fatal error。
- 主码流遇到 D3D11/硬解相关错误时，主视图尝试回退预览码流。
- 离线视频导入前校验失败时阻止导入；本地文件不存在时视频控件显示“文件不存在”。
- 历史复盘引用的离线文件不存在时，回看会停止主播放器、切回采集页并提示恢复原文件或重新导入。
- RTSP/网络视频定位片段时提示不支持自动定位，不把该情况视为播放错误。
- 离线视频文件被移动或删除后，开始采集会提示重新导入。
- 离线视频文件在导入后被替换或修改，开始采集会提示重新导入，避免使用未校验文件。

## 边界情况

- 同一 URL 被多个控件使用时，`StreamRegistry` 会复用同一个 `RtspStream`。
- 带 `videoClipStartMs` 的本地文件回看会创建独立 `RtspStream`，避免 seek 影响共享播放流。
- 暂停播放会释放控件持有的流引用，不是向 `RtspStream` 发送 pause。
- 没有控件引用后，共享流会随 `shared_ptr` 生命周期结束。
- 离线视频暂停后再次开始会重新打开文件；普通播放不保存进度，历史复盘“定位片段”只在打开时按保存的片段起点做一次初始 seek。
