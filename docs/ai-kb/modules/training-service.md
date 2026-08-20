# 训练服务模块

[模块地图](README.md) · [HTTP API](../references/external-apis.md) · [数据模型](../references/database-schema.md)

## 职责

`server/` 提供 FastAPI 训练服务和 PostgreSQL 数据边界：

- JWT 登录与普通业务认证。
- 运动员、教练、ReID 样本/embedding、比赛、场次和动作标准。
- 通用分析任务、单视频任务和完整帧率 batch/run/source/chunk 控制面。
- 训练 session、参与者、视频资产、历史兼容动作/姿态、轨迹/速度/关节指标。
- 人工复核、教练批注、趋势和 baseline。
- NAS 分块范围读取、完整性检查和 run 激活。

## 关键文件

- `server/app/main.py`：配置、认证、路由、SQL、seed 和业务状态机。
- `server/app/schema.py`：完整开发期 PostgreSQL schema 与 `BUSINESS_TABLES`。
- `server/app/analysis_artifacts.py`：NAS URI、gzip JSONL、SHA256、范围与缺口。
- `server/tests/`：协议和 schema 合同测试。
- `tools/reset_postgres_schema.py`：破坏性空库重建。
- `tools/backfill_*.py`：已有开发库幂等回填。
- `tools/import_sqlite_to_postgres.py`：旧 SQLite 一次性导入。

## 生命周期

- 模块导入时必须有 `ISKATING_DATABASE_URL`。
- FastAPI startup 调用 `seed_defaults()`；schema 必须预先创建。
- seed 只插入缺失默认管理员、人员、类别和标准，不覆盖已存在标准。
- 请求依赖在成功后 commit，异常时 rollback。

## 认证

- `/health` 和 `/auth/login` 无 bearer 要求。
- 普通业务路由依赖 `require_user`，使用 `Authorization: Bearer ...`。
- worker 路由依赖 `X-Analysis-Worker-Token`。
- 当前定义了 `require_admin()`，但没有形成完整路由级 RBAC；不要假设 role 已限制业务操作。

## 数据写入约定

- 对外 JSON 为 camelCase，SQL/schema 为 snake_case。
- 人员、教练、比赛等历史引用实体使用 `active` 软归档。
- ReID 样本文件位于 gallery root，数据库保存路径、版本和 embedding。
- 完整逐帧结果不写 PostgreSQL，只写 NAS；数据库保存 chunk 索引。
- 保存 session 时重建 participant、视频和结果子集，并按 athleteId 尝试补 participantId。
- 人工复核保留 AI 原始值，查询和重算使用 effective value。

## API/schema 修改清单

1. 修改 `schema.py`；新表加入 `BUSINESS_TABLES`。
2. 修改 `main.py` 的校验、SQL、序列化、查询和认证。
3. 修改 `trainingdomain.h` 的领域类型。
4. 修改 `trainingrepository.*` 请求/响应映射。
5. 修改 reset/backfill/import 工具。
6. 添加或更新 `server/tests`；外键闭环需真实 PostgreSQL 集成测试。
7. 更新 [数据模型](../references/database-schema.md) 和 [HTTP API](../references/external-apis.md)。

## 已知约束

- 没有 Alembic；生产数据迁移流程未定义。
- 默认凭据和 JWT secret 不安全。
- 主 participant UUID 与轨迹/速度存在已知映射缺口。
- 保存完整分析 session 时可从激活 run 派生约 200ms bbox/track/身份摘要，但不派生轨迹、速度、姿态、动作或评分。
- `joint_metrics` 只提供存储/查询/导出，当前 AI 不生产。
- 当前自动测试多为纯逻辑/源码合同，未覆盖完整真实 PostgreSQL API 链路。
