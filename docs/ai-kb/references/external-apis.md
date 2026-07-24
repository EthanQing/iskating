# External APIs

上级入口：[[00-index|AI 知识库索引]]、[[references/README|Reference 地图]]
相关模块：[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]
相关流程：[[flows/video-streaming-flow|视频播放流程]]、[[flows/pose-analysis-flow|运动员检测与身份流程]]
相关服务：[[third-party-services|第三方服务]]

## RTSP 摄像头/视频源

- 服务名称：RTSP 视频源。
- 用途：提供 12 路预览和主视图视频流。
- 调用位置：`src/ui/videoopenglwidget.cpp`, `src/infrastructure/video/rtspstream.cpp`。
- 认证方式：RTSP URL 中的用户名/密码，由 `SharedCameraSettings` 生成。
- 风险点：密码需要日志脱敏；UDP/TCP、编码器和 D3D11VA 支持会影响播放。

## 模型下载与转换

- YOLO26x：从 `https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26x.pt` 下载后由 Ultralytics 导出 ONNX。
- PersonViT：从 TransReID 官方 MSMT17 ViT-Base baseline checkpoint 下载后由 `tools/convert_personvit_msmt17.py` 转换 ONNX。
- 调用位置：`tools/download_athlete_models.ps1`、`tools/convert_personvit_msmt17.py`。
- 认证方式：使用公开下载地址，无 token。
- 风险点：网络不可达、源文件地址变化、模型文件很大、模型许可证和 MSMT17 数据集条款需要遵守。

## 其他外部 API

### FastAPI 训练服务

- 默认地址：`http://127.0.0.1:8000`，可用 `ISKATING_API_BASE_URL` 覆盖。
- 用途：`/health`、登录、人员/ReID、比赛/标准、session/复核/趋势、视频资产、通用任务和完整帧率分析。
- 桌面端认证：账号密码换取 bearer JWT；客户端可保存 token 或使用环境账号自动登录。
- worker 认证：`/analysis-worker/*` 使用独立 worker token。
- 风险：当前只校验有效用户/token，未按 role 做业务路由 RBAC；默认密码和 JWT secret 不能用于生产。

### NAS 分析资产

- 跨主机视频使用受根目录约束的 `nas://` URI；Windows 和 Ubuntu 分别配置物理根映射。
- DeepStream worker 将 10 秒 gzip JSONL 分块写入 NAS，再向 FastAPI 登记 SHA256 和范围索引。
- NAS 是共享文件系统依赖，不是对象存储 API。

当前未发现支付、邮件、外部对象存储或监控 API。
