# HTTP API Reference

[Reference 地图](README.md) · [训练服务](../modules/training-service.md) · [完整分析流程](../flows/fullrate-analysis.md)

> 路由和参数最终以 `server/app/main.py` 为准。本页列出当前路由组和安全边界，不复制完整响应 schema。

## 基础与认证

| 方法 | 路径 | 认证 | 用途 |
|---|---|---|---|
| GET | `/health` | 无 | 数据库健康检查 |
| POST | `/auth/login` | 无 | 用户名/密码换 bearer JWT |

普通业务请求使用 `Authorization: Bearer <token>`。当前 token 有效期 12 小时。业务路由验证用户 active，但尚无完整 role 级 RBAC。

## 人员与 ReID

- `GET/POST /athletes`
- `PATCH /athletes/{athlete_id}`
- `GET/POST /athletes/{athlete_id}/identity-samples`
- `GET /athletes/{athlete_id}/identity-gallery`
- `POST /athletes/{athlete_id}/identity-samples/{sample_id}/embedding`
- `GET .../{sample_id}/file`
- `DELETE .../{sample_id}`
- `GET/POST /coaches`
- `PATCH /coaches/{coach_id}`
- `GET /coaches/{coach_id}/athletes`

样本上传使用 base64；文件写 gallery root，embedding/版本写 PostgreSQL。

## 比赛、标准与训练任务

- `GET/POST/PATCH /competitions[/{id}]`
- `GET/POST/PATCH /competition-events[/{id}]`
- `GET/POST/PATCH /event-athletes[/{id}]`
- `GET/POST /action-standards`
- `POST /training/tasks/ensure-daily`

PATCH 归档通常通过 `active` 语义，不应硬删历史引用。

## 通用分析任务

- `POST /analysis-tasks`
- `GET /analysis-tasks`
- `POST /offline-analysis/tasks`
- `GET /offline-analysis/tasks`

通用 task progress 是 0–100。单视频任务完成表示探测/登记，不表示逐帧分析完成。

## 完整帧率分析（用户端）

- `POST /offline-analysis/batches`
- `GET /offline-analysis/batches/{batch_id}`
- `POST /offline-analysis/batches/{batch_id}/runs`
- `GET /offline-analysis/runs/{run_id}`
- `POST /offline-analysis/runs/{run_id}/cancel`
- `POST /offline-analysis/runs/{run_id}/retry`
- `POST /offline-analysis/runs/{run_id}/activate`
- `GET /offline-analysis/runs/{run_id}/frames`

这些接口使用普通 bearer JWT。frames 范围查询可能返回 gaps；消费者必须在 gap 清空陈旧结果。

## Worker API

请求头：`X-Analysis-Worker-Token`。

- `POST /analysis-worker/runs/claim`
- `POST /analysis-worker/runs/{run_id}/heartbeat`
- `GET /analysis-worker/runs/{run_id}/gallery`
- `POST /analysis-worker/sources/{source_id}/chunks`
- `POST /analysis-worker/sources/{source_id}/finish`

服务端未配置 token 时返回 503；不匹配返回 401。worker token 与用户 JWT 不可混用。

## Session、视频与历史

- `POST /training/sessions`
- `GET /training/sessions`
- `GET /training/repetitions`
- `GET /training/sessions/{session_id}/repetitions`
- `GET /training/sessions/{session_id}/pose-frames`
- `GET /training/sessions/{session_id}/track-points`
- `GET /training/sessions/{session_id}/speed-metrics`
- `GET /training/sessions/{session_id}/joint-metrics`
- `POST /training/repetitions/{repetition_id}/review`
- `POST /training/sessions/{session_id}/manual-repetitions`
- `POST /training/sessions/{session_id}/coach-comment`
- `GET /training/trends`
- `GET /training/baselines`
- `GET /training/video-files`
- `POST /training/video-files/{video_file_id}/cleanup`

Session 保存 payload 可包含 session、repetitions、participantRepetitions、participantPoseFrames、trackPoints、speedMetrics、jointMetrics。修改字段时同步 C++ Repository 映射和 schema。

## 非 HTTP 外部接口

### RTSP

客户端从 QSettings 组装 URL，优先 UDP 后 TCP。凭据位于 URL，日志必须脱敏。

### NAS

`nas://` 是共享文件系统逻辑 URI，不是对象存储 API。API/worker/Windows 各自映射到物理根，并必须防路径逃逸。
