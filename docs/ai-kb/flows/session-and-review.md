# Session 保存、历史与复盘流程

[流程地图](README.md) · [训练服务](../modules/training-service.md) · [数据模型](../references/database-schema.md)

## 保存输入

客户端提交：

- session 上下文：主运动员、教练、动作标准、比赛/场次、时间、来源和备注。
- 最多 4 名参与者。
- session 视频资产引用。
- 新训练的 bbox/track/身份兼容摘要。
- 满足条件时的 `trackPoints` 与 `speedMetrics`。
- 旧兼容场景可能包含动作、人工复核、姿态或关节数据。

当前新 person/ReID 训练不应生成伪造 repetition、评分、姿态或关节指标。

## 服务端保存时序

1. 校验主 athlete 和 action standard，确定 source 与完整分析关联。
2. upsert `training_sessions`。
3. 删除并重建该 session 的 participant，主运动员固定首槽。
4. 删除并重建视频资产和结果子表。
5. 按 athleteId 为动作/兼容帧补 participantId。
6. 插入 track point；只有 participantId 属于本次服务端 participant 集合时接受。
7. 用 `(participantId, tMs, cameraId)` 关联 speed metric 到 track point。
8. 若无客户端兼容帧但关联 completed run，则从 NAS 分块派生约 200ms bbox/track/身份摘要。
9. 有动作结果时刷新 baseline；纯 person/ReID session 不污染旧评分 baseline。
10. 若关联通用分析任务，则回填 output session、completed 和 100%。

## 已知数据缺口

服务端会为主运动员生成/选择 participant UUID，而轨迹可能仍引用客户端预生成 UUID，导致轨迹和速度静默跳过。修复前必须通过数据库查询验证外键闭环，详见 [已知风险](../05-pitfalls.md)。

## 历史查询

- session 支持按人员、教练、标准、比赛/场次、来源、保存时间、分数和关键词分页排序。
- 动作查询优先 `participant_repetitions`，旧数据回退 `action_repetitions`。
- 轨迹、速度和关节指标分别查询独立表。
- 旧 `participant_pose_frames` API 保留历史兼容；当前客户端不绘制旧关键点覆盖层。

## 复盘与人工优先

- 人工复核不覆盖 AI 原始字段，而是写 `manual_*` 和 review 元数据。
- 展示、报告和 session 重算应使用 effective value：有人工值时优先，否则使用 AI 原始值。
- 新增手动动作、保存复核后重算 session 汇总和 baseline。
- RTSP/NVR 仅提供可用源和片段提示；本地文件才可靠支持精确 seek、慢放和逐帧。

## 视频资产

- RTSP 训练通常登记 `planned`：只有规范路径/引用，没有真实录像。
- 单视频导入登记 `external`：引用原文件，不复制。
- 清理只删除用户确认且位于配置根目录内的本机文件，并写 metadata；session 和索引保留。

## 报告与趋势

- 可导出 session 摘要、动作明细和专项指标。
- 轨迹/速度有当前业务语义；关节指标只有已有数据时才可报告。
- 新 person/ReID session 的零动作/零评分不应被解读为技术能力变差。

## 验收重点

- API 返回成功之外，核对 session、participant、视频、track、speed 的实际行和外键。
- 归档人员后，新选择列表隐藏但历史仍可查询。
- 人工修正后历史、报告、汇总和 baseline 一致。
- 本地文件丢失时历史摘要仍可用，回放给出明确 fallback/错误。
