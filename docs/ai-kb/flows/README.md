# Flows

上级入口：[[../00-index|AI 知识库索引]]

本目录记录当前最重要的业务/数据流程。

## 当前流程

- [[main-user-flow|主用户流程]]: 从配置摄像头到采集、保存记录、查看建议的主流程。
- [[video-streaming-flow|视频播放流程]]: RTSP 视频接入、解码、显示和 fallback。
- [[pose-analysis-flow|运动员检测与身份流程]]: 视频帧进入 YOLO26x/PersonViT 并回写 UI 的流程。
- [[training-record-flow|训练记录流程]]: 训练记录保存、读取、历史和建议生成流程。

## 对应模块

- [[main-user-flow]] 主要连接 [[../modules/core]], [[../modules/frontend]], [[../modules/persistence]]
- [[video-streaming-flow]] 主要连接 [[../modules/video-streaming]], [[../modules/background-workers]]
- [[pose-analysis-flow]] 主要连接 [[../modules/ai-inference]], [[../modules/pose-analysis]], [[../modules/background-workers]]
- [[training-record-flow]] 主要连接 [[../modules/persistence]], [[../modules/pose-analysis]]
