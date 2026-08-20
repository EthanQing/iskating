# AGENTS.md

本文件定义 AI 编码代理在 `iskating` 仓库中的工作协议。目标是：先定位事实，再做小而可验证的修改，并让代码、测试和知识库保持一致。

## 1. 开始任务

### 简单任务

拼写修正、单个常量调整或用户已明确文件与改法的局部修改，可以直接检查目标文件和调用方。

### 非简单任务

开始前依次执行：

1. 查看 `git status --short --branch`，识别用户已有修改；不得覆盖、还原或顺带提交。
2. 阅读 [`docs/ai-kb/00-index.md`](docs/ai-kb/00-index.md)。
3. 按索引只读取与任务直接相关的模块、流程、Runbook 和 Reference。
4. 检查目标实现、测试、调用方和构建清单，不以知识库代替源码验证。
5. 检查当前可用 Skills；只加载直接相关的 Skill，并在实施前简要说明用途。没有匹配项时明确说明，不加载无关 Skill。

不要默认读取整个知识库，也不要把 `README.md`、历史任务记录或构建产物当成当前实现的唯一事实源。

## 2. 事实优先级

发生冲突时按以下顺序判断：

1. 当前源码、测试和运行结果。
2. 构建/依赖清单与数据契约：`mainwindow.pro`、`build/qmake/*.pri`、`server/app/schema.py`、模型 manifest。
3. `docs/ai-kb/` 当前有效文档。
4. 根目录 `README.md` 和 Git 历史。

发现知识库过期时，在本次任务范围内同步修正；无法确认的内容写入 `docs/ai-kb/07-open-questions.md`，不要把推测写成事实。

## 3. 架构边界

系统由三个可独立运行的部分组成：

- **Windows 桌面端**：Qt 6 / C++20，位于 `src/`，负责 UI、RTSP/D3D11VA、Windows 实时 AI、任务编排和 FastAPI 客户端。
- **训练服务**：FastAPI + PostgreSQL，位于 `server/`，负责认证、业务数据和完整分析协议。
- **完整帧率 worker**：Ubuntu / DeepStream 9，位于 `analysis_worker/`，负责 12 路 NAS 视频逐解码帧分析和分块产物。

重要边界：

- 桌面端不直连 PostgreSQL；训练业务通过 `TrainingRepository` 调用 FastAPI。
- QSettings 只保存本机配置和访问参数，不是训练业务主存储。
- Windows 实时分析与 DeepStream 完整帧率分析是两条不同管线，不得共享“latest frame”语义。
- 当前新训练只生成 person 检测、ReID、单机位 track，以及满足条件时的二维轨迹/速度；不生成新姿态关键点、3D 骨架、自动动作计数或技术评分。
- 服务端旧姿态/动作/评分字段是历史兼容边界，不能据此宣称当前实时能力仍存在。

详细关系见 [`docs/ai-kb/02-architecture.md`](docs/ai-kb/02-architecture.md)。

## 4. 修改规则

### 通用

- 优先小改动，不为“顺手优化”扩大范围。
- 修改前检查调用方；修改后检查错误路径和生命周期。
- 不新增依赖，除非现有能力无法完成任务，并说明原因。
- 不复制已有能力；先搜索 helper、repository、配置结构和测试。
- 不写入或打印凭据、完整 RTSP 密码 URL、JWT、worker token。

### Qt / C++

- 业务源码按 `src/app`、`src/ui`、`src/domain`、`src/application`、`src/infrastructure` 分层；不要放回仓库根目录。
- 新增/删除 C++ 文件时同步更新所在层 `.pri`；新增资源时更新 `resources/iskating.qrc`。
- 不编辑生成文件：`Makefile*`、`.qmake.stash`、`x64/`、`ui_*.h`、`moc_*`、`qrc_*`、`*.engine`。
- QWidget 只能在 UI 线程操作；跨线程结果使用 signal/slot 或 queued invocation。
- 视频和 TensorRT 工作不得阻塞 UI 线程。
- 调整 RTSP 日志时继续使用脱敏 URL。

### FastAPI / PostgreSQL

数据结构或 API 修改必须检查并按需同步：

1. `server/app/schema.py` 与 `BUSINESS_TABLES`。
2. `server/app/main.py` 的读写、校验和认证。
3. `src/domain/trainingdomain.h`。
4. `src/infrastructure/persistence/trainingrepository.*` 的 JSON 映射。
5. 相关 backfill/import 工具与测试。
6. `docs/ai-kb/references/database-schema.md` 或 `external-apis.md`。

当前没有 Alembic。`reset_postgres_schema.py --yes` 会破坏数据，不得在未确认数据库可丢弃时执行。

### AI / 视频 / worker

- 模型输入输出、预处理、阈值和版本以 `models/athlete/athlete_models.json` 及代码为准。
- 模型二进制和 TensorRT engine 不提交 Git。
- 不把检测 ROI、四点场地标定、track 或 ReID 身份混为同一概念。
- 不把单机位 `trackId` 当成跨机位全局 ID；跨机位同人依赖 `athleteId`。
- 完整帧率管线不得为了吞吐启用丢帧、leaky queue 或 YOLO interval。
- `nas://` 路径必须受配置根目录约束，禁止路径逃逸。

## 5. 验证

修改后先运行最小相关验证，再在环境允许时扩展：

- Python 服务/worker：`python -m unittest discover -s server/tests -p "test_*.py"`
- 客户端合同测试：构建并运行 `tests/client/client-tests.pro`
- Qt 改动：至少完成对应 qmake/MSVC 构建；UI、RTSP、GPU 行为按任务做人工验证。
- worker 改动：先跑 Python 测试和 `docker compose ... config`；真实 DeepStream/GPU 行为必须在 Ubuntu NVIDIA 环境验证。
- 文档改动：检查 Markdown 链接、文件路径、命令和 Git diff。

不要声称未执行的测试通过。受平台、GPU、摄像头、PostgreSQL 或 Docker 限制无法验证时，说明未验证范围与风险。测试失败时不得删除测试、降低断言或无理由修改预期来掩盖问题。

## 6. 知识库维护

出现以下变化时更新相关知识库：

- 架构、模块职责或关键流程变化。
- API、表结构、环境变量、模型契约变化。
- 构建、测试、部署或排障步骤变化。
- 新的高风险限制或已确认问题。

文档只写当前事实、可执行步骤和明确状态；临时计划不写入模块事实文档。知识库维护规则见 [`docs/ai-kb/00-index.md`](docs/ai-kb/00-index.md)。

## 7. Git 与交付

任务完成后默认创建本地提交，但不得推送远端、创建 PR 或发布，除非用户明确要求。

提交前：

1. 查看工作区状态，区分本次修改与用户已有修改。
2. 只暂存本次任务相关文件或代码块。
3. 检查 `git diff --cached`，排除生成物、缓存和无关格式化。
4. 关键测试因本次修改失败、存在冲突或无法安全区分修改时，不强行提交，并说明原因。

最终回复简要列出：修改内容、验证结果、知识库更新、实际使用的 Skills、本地提交 hash（或未提交原因）。
