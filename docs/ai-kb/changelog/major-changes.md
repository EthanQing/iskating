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
