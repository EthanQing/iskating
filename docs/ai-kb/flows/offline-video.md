# 单视频导入流程

[流程地图](README.md) · [桌面客户端](../modules/desktop-client.md) · [视频与实时 AI](../modules/video-and-realtime-ai.md)

## 目标

把一个本地视频作为 Windows 主视图来源，并在用户点击“开始采集”后按播放进度复用实时 AI。它不是完整帧率离线分析。

## 时序

1. 用户选择本地文件；客户端保存 `offlineVideo/lastDir`。
2. `AnalysisTaskManager` 创建类型为 `offline_import` 的通用任务并在后台执行。
3. `OfflineVideoProbe` 检查：文件存在/可读/非空、视频轨、时长、seek、D3D11VA 支持和首个硬件帧。
4. 探测成功后通过 FastAPI 保存 `offline_analysis_tasks` 记录。
5. 任务标记 100% completed，并通知 UI 切换主视图到“离线视频 · 文件名”。
6. 此时只完成导入；用户点击“开始采集”后，`AthleteAnalysisManager` 才跟随播放器 latest frame 执行低帧率实时 AI。
7. 保存训练时 session 记录单视频任务、绝对文件引用和 `external` 视频资产。

## 播放语义

- 本地文件使用独立 `RtspStream`，支持 seek、倍率、逐帧和媒体时间查询。
- 离线模式不启动额外 12 路 RTSP 预览，避免占用解码资源。
- 导入后文件被移动、删除或修改时，应要求重新导入。
- 客户端不复制原文件；历史回放依赖原路径仍可访问。

## 任务控制

- 当前进程可暂停、继续或取消探测任务。
- 重启后只恢复 paused 展示，不重建 file/job 参数，不能直接继续。
- 100% 表示“探测 + 服务端登记完成”，不表示整段视频每帧 AI 已完成。

## 失败语义

- 探测失败：任务 failed，不改变当前播放源。
- 训练服务不可用或任务保存失败：不切换到离线模式，避免只有本地 UI 状态没有可追踪任务。
- D3D11VA 不兼容：明确提示编码/GPU 问题；当前无软件 fallback。

## 验收重点

- 导入工作不阻塞 UI。
- 失败时当前视频和设置保持不变。
- 开始采集前没有误报 AI 已完成。
- 保存后的 `analysis_task_id`、`source_type/source_ref` 和视频资产可反查。
