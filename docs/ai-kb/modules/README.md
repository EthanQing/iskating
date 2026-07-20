# Modules

上级入口：[[../00-index|AI 知识库索引]]

本目录记录当前项目可识别的主要模块。后续任务不要默认全读；按任务选择相关模块。

## 当前模块

- [[core|应用核心]]: `MainWindow`、应用生命周期、采集/保存/导航编排。
- [[frontend|Qt Widgets 前端]]: Qt Widgets、QSS、资源和动态 UI。
- [[video-streaming|视频流]]: RTSP、FFmpeg、D3D11VA 和视频显示。
- [[ai-inference|AI 推理]]: 当前主链路是 TensorRT YOLO26x person + PersonViT ReID；旧 3D/手部后端未进入活跃构建。
- [[pose-analysis|旧姿态数据兼容]]: 旧姿态、评分、骨架和轨迹数据的读取边界。
- [[persistence|持久化与训练服务]]: QSettings 本机配置、QtNetwork/FastAPI 和 PostgreSQL 训练业务数据。
- [[background-workers|后台线程]]: 视频与 AI 分析后台线程。
- DeepStream 完整帧率 worker 尚未拆成独立模块文档，当前由 [[../02-architecture|架构说明]] 和 `docs/full-rate-analysis-guide.md` 承接。

## 常用组合

- 视频显示链路：[[video-streaming]] + [[background-workers]] + [[../flows/video-streaming-flow]]
- 运动员识别链路：[[ai-inference]] + [[../flows/pose-analysis-flow]]；旧姿态兼容：[[pose-analysis]]
- 用户操作链路：[[core]] + [[frontend]] + [[persistence]] + [[../flows/main-user-flow]]

## 当前未拆分的模块文档

- `auth.md`: 已有 JWT 登录和 worker token，但无可见登录页且没有 role 级 RBAC。
- `api.md`: FastAPI 路由已存在，当前由 [[../02-architecture|架构说明]]、[[persistence|持久化]] 和 references 文档承接。
- `billing.md`: 未发现支付/订阅模块。
- `notifications.md`: 未发现通知模块。
- `database.md`: PostgreSQL schema 已存在，当前见 [[persistence]] 与 [[../references/database-schema|数据结构]]。
