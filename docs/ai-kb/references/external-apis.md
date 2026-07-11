# External APIs

上级入口：[[00-index|AI 知识库索引]]、[[references/README|Reference 地图]]
相关模块：[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]
相关流程：[[flows/video-streaming-flow|视频播放流程]]、[[flows/pose-analysis-flow|运动员检测与身份流程]]
相关服务：[[third-party-services|第三方服务]]

## RTSP 摄像头/视频源

- 服务名称：RTSP 视频源。
- 用途：提供 12 路预览和主视图视频流。
- 调用位置：`videoopenglwidget.cpp`, `rtspstream.cpp`。
- 认证方式：RTSP URL 中的用户名/密码，由 `SharedCameraSettings` 生成。
- 风险点：密码需要日志脱敏；UDP/TCP、编码器和 D3D11VA 支持会影响播放。

## 模型下载与转换

- YOLO26x：从 `https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26x.pt` 下载后由 Ultralytics 导出 ONNX。
- PersonViT：从 TransReID 官方 MSMT17 ViT-Base baseline checkpoint 下载后由 `tools/convert_personvit_msmt17.py` 转换 ONNX。
- 调用位置：`tools/download_athlete_models.ps1`、`tools/convert_personvit_msmt17.py`。
- 认证方式：使用公开下载地址，无 token。
- 风险点：网络不可达、源文件地址变化、模型文件很大、模型许可证和 MSMT17 数据集条款需要遵守。

## 其他外部 API

TODO: 未发现 HTTP API、支付 API、邮件 API、对象存储 API 或监控 API。
