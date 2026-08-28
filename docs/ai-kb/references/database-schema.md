# 数据模型 Reference

[Reference 地图](README.md) · [训练服务](../modules/training-service.md) · [Session 流程](../flows/session-and-review.md)

> 字段级权威来源是 `server/app/schema.py` 和 `server/app/main.py`。本页描述表组、不变量和维护联动，避免复制完整 DDL。

## 持久化边界

| 存储 | 内容 | 不包含 |
|---|---|---|
| QSettings | 摄像头、本机采集/存储/NAS 映射、API 访问参数 | 新训练业务主数据 |
| PostgreSQL | 人员、标准、任务、session、指标、完整分析索引 | 完整逐帧 JSONL、ReID 图片二进制 |
| Gallery 文件根 | ReID 样本图片 | embedding（在 PostgreSQL） |
| NAS | 12 路视频和完整分析 gzip JSONL chunk | 业务关系与状态索引 |

## Schema 管理

- 完整开发期 DDL：`server/app/schema.py` 中的 `SCHEMA_SQL`。
- reset 删除顺序：`BUSINESS_TABLES`；新增表必须加入。
- FastAPI startup 只 seed，不建表。
- 没有 Alembic；可丢弃开发库用 reset，需要保留的已有库按需用 `tools/backfill_*.py`。
- 旧 SQLite 只通过 `tools/import_sqlite_to_postgres.py` 显式导入。

## 表组

### 身份与人员

- `users`：登录用户、密码 hash、role、active。
- `athletes`、`coaches`：档案；删除语义为 `active=false` 归档。
- `coach_athletes`：教练与运动员关系。
- `athlete_identity_samples`：样本文件元数据和模型/预处理版本。
- `athlete_identity_embeddings`：与 sample 一对一的向量；删除 sample 级联删除。

### 比赛与标准

- `competitions`
- `competition_events`
- `event_athletes`：同一 event + athlete 唯一。
- `action_categories`
- `action_standards`：版本、目标、阈值、权重、提示和参考视频。
- `training_plans`、`training_tasks`

### 通用与完整分析任务

- `analysis_tasks`：类型 `offline_import|full_rate_batch`；状态 `queued|running|paused|completed|failed|cancelled`；progress 合同为 0–100。
- `offline_analysis_tasks`：单视频任务，也承载完整 batch 的每路源信息。
- `offline_analysis_batches`：12 路批次与 active run。
- `offline_analysis_runs`：一次执行版本、标签、状态、租约和进度。
- `offline_analysis_run_sources`：每路状态、帧/PTS/checkpoint。
- `offline_analysis_result_chunks`：artifact URI、帧/时间范围、SHA256、schema version。

完整逐帧内容只在 NAS。run/source 的状态范围以 DDL CHECK 和 `main.py` 状态机为准，不从 UI 文案推断。

### Session 与视频

- `training_sessions`：主运动员、训练/比赛上下文、汇总、来源、视频引用和分析关联。
- `training_session_participants`：最多 4 名；session 内 athlete 和 slot 各自唯一，主运动员为 slot 1。
- `training_video_files`：session 视频资产；状态 `planned|external|recorded`。

关键语义：

- `planned` 不保证文件存在。
- `external` 引用原始本地文件，不复制。
- 清理本机文件只写 metadata，不删除 session/资产行。
- 完整分析 batch/run 可与 session 关联，但激活不会自动创建 session。

### 当前指标与兼容结果

- `track_points`：二维米制轨迹权威表；唯一 `(participant_id, t_ms, camera_id)`。
- `speed_metrics`：通过 `track_point_id` 与轨迹点一对一；单位通常 `m/s`。
- `joint_metrics`：participant/time/camera/joint/side 唯一；当前 AI 不生成。
- `participant_pose_frames`：历史兼容时间线；新数据主要是约 200ms bbox/track/身份摘要，不是权威轨迹。
- `action_repetitions`：旧动作兼容表。
- `participant_repetitions`：多人动作结果权威路径；旧 session 可回退上一表。
- `athlete_action_baselines`：按运动员/标准汇总旧动作能力。

## 轨迹/速度不变量

- 坐标必须来自有效四点标定，单位为米；首版 `z=0`。
- `t_ms` 是 session 相对毫秒。
- point participant 必须属于当前 session。
- speed participant/time/camera 必须与对应 point 一致。
- 首点或时间异常可以保存为 `valid=false`，不能伪造有效速度。
- 当前 participant UUID 映射可能导致静默漏存，见 [已知风险](../05-pitfalls.md)。

## 历史兼容不变量

- 新 person/ReID session 不填充虚假 pose、repetition 或 score。
- 人工复核写独立 manual 字段，AI 原始值保留。
- 查询/报告优先 effective value。
- 无 repetition 的新 session 不刷新旧动作 baseline。

## 变更检查表

1. DDL 与 `BUSINESS_TABLES`。
2. API 写入、读取、筛选、序列化和认证。
3. C++ domain struct 与 Repository JSON。
4. reset/backfill/import/seed。
5. Python schema test + 真实 PostgreSQL 外键测试。
6. 本页与 HTTP API Reference。
