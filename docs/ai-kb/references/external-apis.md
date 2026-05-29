# External APIs

上级入口：[[00-index|AI 知识库索引]]、[[references/README|Reference 地图]]
相关模块：[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]
相关流程：[[flows/video-streaming-flow|视频播放流程]]、[[flows/pose-analysis-flow|姿态分析流程]]
相关服务：[[third-party-services|第三方服务]]

## RTSP 摄像头/视频源

- 服务名称：RTSP 视频源。
- 用途：提供 12 路预览和主视图视频流。
- 调用位置：`videoopenglwidget.cpp`, `rtspstream.cpp`。
- 认证方式：RTSP URL 中的用户名/密码，由 `SharedCameraSettings` 生成。
- 风险点：密码需要日志脱敏；UDP/TCP、编码器和 D3D11VA 支持会影响播放。

## Hugging Face 模型下载

- 服务名称：Hugging Face。
- 用途：下载 RTMW3D-x ONNX 模型。
- 调用位置：`tools/download_rtmw3d_x.ps1`。
- 认证方式：当前脚本使用公开 URL，无 token。
- 风险点：网络不可达、URL 变更、模型文件很大。

## 其他外部 API

TODO: 未发现 HTTP API、支付 API、邮件 API、对象存储 API 或监控 API。
