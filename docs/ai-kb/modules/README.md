# Modules

上级入口：[[../00-index|AI 知识库索引]]

本目录记录当前项目可识别的主要模块。后续任务不要默认全读；按任务选择相关模块。

## 当前模块

- [[core|应用核心]]: `MainWindow`、应用生命周期、采集/保存/导航编排。
- [[frontend|Qt Widgets 前端]]: Qt Widgets、QSS、资源和动态 UI。
- [[video-streaming|视频流]]: RTSP、FFmpeg、D3D11VA 和视频显示。
- [[ai-inference|AI 推理]]: TensorRT runner、人体/3D/手部模型后端。
- [[pose-analysis|旧姿态数据兼容]]: 旧姿态、评分、骨架和轨迹数据的读取边界。
- [[persistence|本地持久化]]: QSettings 配置和训练历史。
- [[background-workers|后台线程]]: 视频与 AI 分析后台线程。

## 常用组合

- 视频显示链路：[[video-streaming]] + [[background-workers]] + [[../flows/video-streaming-flow]]
- 运动员识别链路：[[ai-inference]] + [[../flows/pose-analysis-flow]]；旧姿态兼容：[[pose-analysis]]
- 用户操作链路：[[core]] + [[frontend]] + [[persistence]] + [[../flows/main-user-flow]]

## 当前未识别出的模块

- `auth.md`: 未发现应用登录/权限模块。
- `api.md`: 未发现 HTTP API 或后端路由。
- `billing.md`: 未发现支付/订阅模块。
- `notifications.md`: 未发现通知模块。
- `database.md`: 未发现 SQL 数据库；本地持久化见 [[persistence]]。
