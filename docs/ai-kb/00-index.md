# AI 知识库索引

这是 Codex 和维护者进入项目时的导航页。不要默认全量读取知识库；先读本页，再按任务选择最相关的模块、流程或 runbook。

## 使用方式

- 先读本页，再按任务进入一个分类 MOC：[[modules/README|模块地图]]、[[flows/README|流程地图]]、[[runbooks/README|Runbook 地图]]、[[references/README|Reference 地图]]。
- 需要全局背景时只补读 [[01-project-overview|项目概览]] 和 [[02-architecture|架构说明]]。
- 遇到不确定或风险判断时读 [[05-pitfalls|坑点]] 与 [[07-open-questions|未确认问题]]。
- 不要为了一个小任务全量读取 `docs/ai-kb/`。

## 当前有效文档

- [[01-project-overview|项目概览]]
- [[02-architecture|架构说明]]
- [[03-commands|运行命令]]
- [[04-conventions|代码约定]]
- [[05-pitfalls|易踩坑清单]]
- [[06-glossary|术语表]]
- [[07-open-questions|未确认问题]]

## 产品功能盘点

- [[../feature-inventory|当前功能全量清单]]：供产品、界面和使用流程重新编排使用。
- [客户端九区流程 HTML 原型](../client-flow-prototype.html)：单文件交互示意，不代表 Qt 客户端已完成导航重构。

## 模块入口

- [[modules/README|模块总览]]
- [[modules/core|主窗口与应用核心]]
- [[modules/frontend|Qt Widgets 界面]]
- [[modules/video-streaming|RTSP/D3D 视频链路]]
- [[modules/ai-inference|TensorRT 推理]]
- [[modules/pose-analysis|旧姿态数据兼容模块]]
- [[modules/persistence|持久化与训练服务]]
- [[modules/background-workers|后台线程]]

## 流程入口

- [[flows/README|流程总览]]
- [[flows/main-user-flow|主用户流程]]
- [[flows/video-streaming-flow|视频播放流程]]
- [[flows/pose-analysis-flow|运动员检测与身份流程]]
- [[flows/training-record-flow|训练记录流程]]

## 运维与调试入口

- [[runbooks/local-development|本地开发]]
- [[runbooks/testing|测试]]
- [[runbooks/deployment|部署]]
- [[runbooks/debugging|调试]]

## References

- [[references/environment-variables|环境变量与本机路径]]
- [[references/database-schema|数据持久化结构]]
- [[references/external-apis|外部 API]]
- [[references/third-party-services|第三方服务]]

## 按任务最小读取路径

- 视频/RTSP/D3D 问题：[[modules/video-streaming]] -> [[flows/video-streaming-flow]] -> [[runbooks/debugging]]
- AI/TensorRT/模型问题：[[modules/ai-inference]] -> [[flows/pose-analysis-flow]] -> [[references/third-party-services]]
- UI/QSS/窗口交互：[[modules/frontend]] -> [[modules/core]] -> [[04-conventions]]
- 产品功能/UX 流程：[[../feature-inventory|全量功能盘点]] -> [HTML 原型](../client-flow-prototype.html) -> [[flows/main-user-flow]]
- 训练记录/配置问题：[[modules/persistence]] -> [[flows/training-record-flow]] -> [[references/database-schema]]
- 构建/部署问题：[[03-commands]] -> [[runbooks/local-development]] 或 [[runbooks/deployment]] -> [[references/environment-variables]]

## 不应默认读取的目录

- [[archive/README|archive]]: 历史归档，只在追溯旧结论时读取。
- [[tasks/README|tasks]]: 任务跟踪，不是架构事实来源。
- [[changelog/README|changelog]]: 知识库变更摘要，不替代模块文档。
- [[prompts/README|prompts]]: 给 Codex 的提示词模板，只有发起对应工作流时读取。
