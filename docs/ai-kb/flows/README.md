# 流程地图

[返回知识库索引](../00-index.md)

| 流程 | 起点 | 终点 | 主要模块 |
|---|---|---|---|
| [实时训练](realtime-training.md) | RTSP/活动视频流 | UI 结果与待保存内存数据 | 桌面客户端、视频与实时 AI |
| [单视频导入](offline-video.md) | 本地视频文件 | 已登记任务、播放与跟随播放分析 | 桌面客户端、训练服务 |
| [完整帧率分析](fullrate-analysis.md) | 12 路 `nas://` manifest | 可查询/激活的 NAS 分块结果 | 桌面客户端、训练服务、worker |
| [Session 保存与复盘](session-and-review.md) | 内存训练上下文或激活 run | PostgreSQL session、历史、报告 | 桌面客户端、训练服务 |

流程文档只描述跨模块时序和失败语义；类职责见 [模块地图](../modules/README.md)，字段/API 见 [Reference](../references/README.md)。
