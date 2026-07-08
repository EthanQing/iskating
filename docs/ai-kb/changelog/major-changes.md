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
- 新增 F-04 摄像头配置模板：系统设置支持导入/导出 JSON 模板，批量交换公共 RTSP 参数、预览/主码流路径、NVR 模板、12 路 IP 和场地标定，导入先摘要确认并继续通过 QSettings 持久化。
- 新增 F-05 批量连通测试：系统设置可按当前表单逐路测试 RTSP 预览流，显示成功/失败、UDP/TCP、分辨率、帧率和错误原因，结果仅本次展示且不写回配置。

## 2026-07-07

- 完成 F-01 Release 产物盘点：在部署 Runbook 中明确 `x64/Release` 发布目录的必要交付物、条件交付物和不应作为必要交付物的构建/临时文件边界。
- 完成 F-16 比赛基础信息：新增 `competitions` PostgreSQL 表和 `training_sessions.competition_id`，桌面端支持比赛管理、训练归属比赛、历史按比赛筛选，以及 Markdown/CSV/PDF 报告展示比赛信息。
- 完成 F-17 场次分组与参赛关系：新增 `competition_events`、`event_athletes` 和训练 session 场次/参赛外键，桌面端比赛管理可维护场次/分组与参赛运动员，采集页可按比赛选择场次，历史筛选和 Markdown/CSV/PDF 报告展示场次、分组、参赛号、道次、成绩和名次。
- 完成 F-18 分析结果归属：新增 session 级 `source_type/source_ref`，训练记录自动归属为训练、比赛或导入视频，历史页支持来源筛选，历史卡和 Markdown/CSV/PDF 报告展示分析归属。
- 完成 F-20 动作明细检索与导出：新增跨 session 动作实例检索 API 和历史页动作检索对话框，支持按有效性、错误项、分数、时间和片段筛选，并可导出 CSV/XLSX；引入 vendored QXlsx 用于 XLSX 写入。
- 完成 F-21 多人承接身份/轨迹协议：新增 session 参与者表和动作级身份轨迹字段，采集页支持最多 4 名参与运动员与人工轨迹绑定，关键帧、动作检索和导出展示运动员身份、轨迹 ID、机位和帧时间。
- 开发期数据库策略切换为空库重建：移除 Alembic 增量迁移链，新增 `server/app/schema.py` 当前完整 schema 和 `tools/reset_postgres_schema.py --yes` 手动重建脚本；后续数据库字段变化先更新当前 schema，不做历史数据迁移。
- 完成 F-13 离线导入文件校验：导入本地视频前校验文件、视频轨、时长、seek 能力和 D3D11VA 首帧硬解，失败时提前展示原因并保留当前播放源。

## 2026-07-08

- 完成 F-09 视频存储规范与训练记录关联：新增 `training_video_files`，训练保存时登记主视频/主机位的视频序号、规范目录、文件名、元数据路径和 `planned/external/recorded` 状态。
- 历史卡片和 Markdown/CSV/PDF 报告展示视频资产信息；当前不从 RTSP 实际录制或复制视频文件，回放仍沿用 NVR、RTSP 引用或离线原文件。
- 完成 F-10 录像索引入库：`training_video_files` 增加时间覆盖范围、文件大小/修改时间和校验预留字段，`action_repetitions` 通过 `video_file_id/video_index` 关联动作片段到视频资产。
- 新增 `tools/backfill_video_indexes.py`，用于已有 PostgreSQL 开发库幂等补齐视频索引字段和旧记录关联；报告和动作检索展示视频序号、状态、路径和片段时间。
