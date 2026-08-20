# 模块地图

[返回知识库索引](../00-index.md)

知识库按运行时边界划分四个模块：

| 模块 | 职责 | 主要目录 |
|---|---|---|
| [桌面客户端](desktop-client.md) | Qt UI、应用编排、本机配置、任务中心、API 客户端 | `src/app`、`src/ui`、`src/domain`、`src/application`、部分 `src/infrastructure` |
| [视频与实时 AI](video-and-realtime-ai.md) | RTSP/本地视频、D3D11VA、YOLO、ROI、track、ReID、轨迹速度 | `src/infrastructure/video`、`src/infrastructure/inference`、`src/application/athleteanalysismanager.*` |
| [训练服务](training-service.md) | FastAPI、JWT、PostgreSQL、业务 API、完整分析控制面 | `server/` |
| [完整帧率 Worker](fullrate-worker.md) | DeepStream 数据面、worker 租约、NAS 结果分块 | `analysis_worker/` |

模块之间的时序见 [流程地图](../flows/README.md)。配置、表和 API 清单见 [Reference](../references/README.md)。
