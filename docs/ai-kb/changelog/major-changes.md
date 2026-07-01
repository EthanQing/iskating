# Major Changes

上级入口：[[00-index|AI 知识库索引]]、[[changelog/README|变更记录地图]]
相关记录：[[tasks/done|已完成事项]]、[[decisions/initial-project-analysis|首次项目分析决策]]
相关文档：[[01-project-overview|项目概览]]、[[02-architecture|架构说明]]、[[05-pitfalls|坑点]]

## 2026-05-28

- 新增面向 Obsidian + Codex 的 AI 知识库。
- 记录项目为 Qt/C++ Windows 桌面应用，而非 Web 项目。
- 识别核心模块：主窗口编排、Qt Widgets UI、RTSP/D3D11 视频、TensorRT 推理、姿态评分、本地持久化、后台线程。
- 记录高风险区域：构建环境强绑定、D3D11VA 硬解、TensorRT engine 缓存、QSettings 兼容和模型分发。
- 补充 Obsidian 双链结构：`00-index.md` 作为中心节点，各分类 README 作为 MOC，具体模块/流程/runbook/reference 文档连接到直接相关文档，减少后续 AI 任务需要读取的文档数量。
- 新增 [[tasks/product-feature-backlog|产品功能缺口待办]]，完整记录面向滑冰运动员日常训练动作标准系统的未做功能。

## 2026-06-30

- 将训练业务数据库从本机 SQLite 运行时替换为 FastAPI + PostgreSQL 服务端架构。
- 新增 Alembic PostgreSQL 初始迁移、FastAPI 训练服务、基础登录/JWT、默认 seed 和训练业务 REST API。
- `TrainingRepository` 保留原 C++ 同步接口，内部改为 QtNetwork 调用训练服务；桌面端不再创建或升级本机 `iskating.db`。
- 新增 `tools/import_sqlite_to_postgres.py`，用于切换前一次性导入旧 SQLite 训练数据。
- `mainwindow.pro` 移除 QtSql/SQLite driver 部署，改用 QtNetwork。
- 增强 RTSP 断流重连诊断：记录 UDP/TCP fallback、连续/总重连次数、断流/恢复时间、长时间断流状态和脱敏日志。

## 2026-07-01

- 新增 F-08 NVR 回放 MVR：系统设置支持 NVR RTSP 回放模板，历史页和复盘校准可按训练开始时间、机位 IP 与动作片段窗口生成 NVR 回放 URL。
- 训练历史 API 返回已有 `started_at`，桌面端不新增数据库字段即可生成训练时间段回放。
