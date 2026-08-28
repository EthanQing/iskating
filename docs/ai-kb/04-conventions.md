# 工程约定

[返回索引](00-index.md) · [系统架构](02-architecture.md) · [已知风险](05-pitfalls.md)

## 目录与构建

- `src/app`：进程入口与生命周期装配。
- `src/ui`：Qt Widgets、Designer UI、对话框和展示组件。
- `src/domain`：领域数据结构，不依赖具体基础设施。
- `src/application`：跨模块流程和后台任务编排。
- `src/infrastructure`：视频、推理、持久化和配置实现。
- 每层通过同层 `.pri` 汇总；新增/删除 C++ 文件时必须同步清单。
- 新资源放 `resources/icons|images|styles` 并登记到 `resources/iskating.qrc`。
- 不修改 `Makefile*`、`.qmake.stash`、`x64/`、Qt 生成源码和 TensorRT engine。

## C++ / Qt 风格

- 类和 struct：`PascalCase`；成员：`m_` 前缀；常量：`k` 前缀。
- 头文件使用 include guard；`.cpp` 先包含自身头文件。
- 文件内 helper 优先放匿名 namespace，避免无必要的公共抽象。
- 低层可失败操作通常返回 `bool` 并通过 `QString *errorMessage` 传递错误。
- 后台结果通过 signal/slot、callback 和 `Qt::QueuedConnection` 回 UI。
- 不从 worker 线程访问 QWidget，不在持有 mutex 时调用长耗时或可能回调 UI 的代码。

## UI 与资源

- 稳定基础布局修改 `src/ui/mainwindow.ui`；复杂动态区域可在 `.cpp` 装配。
- 优先用 objectName、动态 property 和 QSS 表达状态；property 改变后按现有 `repolish()` 模式刷新。
- 图标按钮和窄区域保持固定尺寸；悬浮提示不要通过增删按钮文字造成布局抖动。
- 长机位名、URL、反馈和路径需 elide 或 word wrap。
- 不直接编辑 `ui_mainwindow.h`。

## 视频与日志

- RTSP URL 记录前必须脱敏，不能输出用户名/密码组合后的完整 URL。
- 同 URL 的实时流由 `StreamRegistry` 复用；带 seek 的本地回放使用独立流，避免互相改变位置。
- 错误文案面向用户保持简短，诊断细节进入日志。
- 修改重连、stop、D3D11 device 生命周期时必须检查线程退出和共享引用。

## AI 契约

- 模型 shape、阈值、预处理、版本以 `models/athlete/athlete_models.json` 为主。
- ROI 在 YOLO 后、track 前执行；四点标定在身份结果后生成场地坐标，两者不是同一配置。
- `trackId` 只在单机位范围内有意义；`athleteId` 表示匹配身份。
- 模型降级：缺 YOLO 时无 AI；缺 PersonViT 时保留 person bbox、身份为 unknown；视频播放应尽量保持。
- 不为旧兼容表生成虚假姿态、动作或评分。

## FastAPI / 数据

- 桌面端通过 `TrainingRepository` 调用 API，不直接连接 PostgreSQL。
- API JSON 对外使用 camelCase，数据库列使用 snake_case；新增字段需同步双向映射。
- API/schema 变更同时检查：`server/app/schema.py`、`server/app/main.py`、领域 struct、Repository、工具、测试和 Reference。
- 新表必须加入 `BUSINESS_TABLES`，否则 reset 无法可靠重建。
- 人员/比赛等被历史引用的数据优先软归档，不硬删。
- 人工复核遵循“AI 原始值保留、人工有效值优先展示/汇总”。

## Python

- 保持现有标准库 + FastAPI/SQLAlchemy 风格，不为简单逻辑新增框架。
- NAS URI 解析必须 resolve 后检查仍位于配置根目录内。
- 分块先完整写入临时文件、原子重命名，再计算/登记校验值。
- 错误不得被吞掉后伪装为完成状态。

## 测试与文档

- 修改行为时优先补最接近该契约的测试；不要只修改文档描述。
- 测试预期应表达产品/协议要求，不为错误实现降级断言。
- 文档使用相对 Markdown 链接；命令注明执行目录、平台和破坏性。
- 一个事实只在一个主要文档详细定义，其他位置链接引用。

## Git

- 只提交本次任务相关内容；已有未提交修改不得还原、暂存或顺带格式化。
- 默认创建本地提交；未经用户明确要求，不 push、建 PR 或部署。
- `origin` 为 `https://github.com/EthanQing/iskating.git`，`main` 是受保护的默认分支。
- 所有改动在非 `main` 分支完成并通过 Pull Request 合并；禁止直接推送、强制推送或删除 `main`。
- 重要版本或基线使用 annotated tag，且 tag 应指向已经验证并合并的提交。
