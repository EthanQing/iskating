# iSkating AI 知识库

> 当前有效知识库入口。内容基于代码提交 `541e7b6` 重新核对；代码继续演进时，以当前源码和测试为最高事实源。

## 如何使用

1. 先读本页，确定任务属于哪个模块和流程。
2. 一般只再读 **1 个模块文档 + 1 个流程文档 + 必要的 Runbook/Reference**。
3. 修改前回到源码、测试和清单验证，不以文档代替代码。
4. 发现不一致时同步更新文档；不确定项记录到 [未确认问题](07-open-questions.md)。

无需为拼写、单个常量或已明确的局部修改读取整个知识库。

## 全局入口

- [项目概览](01-project-overview.md)：产品边界、当前能力、目录入口。
- [系统架构](02-architecture.md)：三进程架构、数据与线程边界。
- [命令速查](03-commands.md)：构建、测试、服务和工具命令。
- [工程约定](04-conventions.md)：目录、代码、API、数据和文档约定。
- [已知风险](05-pitfalls.md)：修改前必须关注的高风险限制。
- [术语表](06-glossary.md)：项目专用概念。
- [未确认问题](07-open-questions.md)：尚未由代码或产品决策关闭的问题。

## 模块地图

- [桌面客户端](modules/desktop-client.md)：Qt UI、应用编排、配置、任务中心、Repository。
- [视频与实时 AI](modules/video-and-realtime-ai.md)：FFmpeg/D3D11VA、YOLO、ROI、track、ReID、轨迹速度。
- [训练服务](modules/training-service.md)：FastAPI、JWT、PostgreSQL、历史与报告数据。
- [完整帧率 Worker](modules/fullrate-worker.md)：DeepStream、NAS 分块、租约和激活。

完整列表见 [模块地图](modules/README.md)。

## 流程地图

- [实时训练](flows/realtime-training.md)
- [单视频导入](flows/offline-video.md)
- [12 路完整帧率分析](flows/fullrate-analysis.md)
- [Session 保存、历史与复盘](flows/session-and-review.md)

完整列表见 [流程地图](flows/README.md)。

## Runbook

- [本地开发](runbooks/local-development.md)
- [测试与验收](runbooks/testing.md)
- [部署](runbooks/deployment.md)
- [排障](runbooks/debugging.md)

## Reference

- [环境变量与本机配置](references/environment-variables.md)
- [数据模型](references/database-schema.md)
- [HTTP API](references/external-apis.md)
- [第三方依赖与模型](references/third-party-services.md)

## 按任务选择最小阅读路径

| 任务 | 建议路径 |
|---|---|
| Qt 页面、交互、QSS | [桌面客户端](modules/desktop-client.md) → [工程约定](04-conventions.md) |
| RTSP、D3D11VA、seek、断流 | [视频与实时 AI](modules/video-and-realtime-ai.md) → [实时训练](flows/realtime-training.md) → [排障](runbooks/debugging.md) |
| YOLO、ROI、track、ReID | [视频与实时 AI](modules/video-and-realtime-ai.md) → [实时训练](flows/realtime-training.md) → [第三方依赖](references/third-party-services.md) |
| FastAPI 路由或认证 | [训练服务](modules/training-service.md) → [HTTP API](references/external-apis.md) |
| PostgreSQL/schema | [训练服务](modules/training-service.md) → [数据模型](references/database-schema.md) → [测试](runbooks/testing.md) |
| 单视频导入/任务中心 | [单视频导入](flows/offline-video.md) → [桌面客户端](modules/desktop-client.md) |
| DeepStream/完整分析 | [完整帧率 Worker](modules/fullrate-worker.md) → [完整分析流程](flows/fullrate-analysis.md) → [部署](runbooks/deployment.md) |
| 训练保存、轨迹/速度、历史 | [Session 流程](flows/session-and-review.md) → [数据模型](references/database-schema.md) → [已知风险](05-pitfalls.md) |
| 构建、测试、部署 | [命令速查](03-commands.md) → 对应 Runbook |

## 文档边界与维护规则

- **模块文档**写职责、关键文件、接口和修改联动。
- **流程文档**写跨模块时序、状态和失败语义。
- **Runbook**写可执行步骤和验收方法。
- **Reference**写稳定清单与契约，不写长篇设计讨论。
- **未确认问题**只收录尚未关闭且会影响实现的事项。
- 临时任务、提示词模板和重复 changelog 不属于知识库事实层；历史追溯使用 Git。
- 一个事实只指定一个主要落点，其他文档使用链接和短摘要，避免复制整段内容。
