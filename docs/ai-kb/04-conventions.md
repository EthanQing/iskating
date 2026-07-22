# Conventions

上级入口：[[00-index|AI 知识库索引]]
相关文档：[[02-architecture|架构说明]]、[[05-pitfalls|坑点]]、[[06-glossary|术语表]]
相关模块：[[modules/core|应用核心]]、[[modules/frontend|Qt Widgets 前端]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]
任务提示：[[prompts/codex-review-code|代码审查提示词]]、[[prompts/codex-refactor|重构提示词]]

## 命名规则

- Qt 类使用 PascalCase，例如 `MainWindow`, `VideoOpenGLWidget`, `RtspStream`。
- 成员变量使用 `m_` 前缀，例如 `m_cameraButtons`, `m_isRecording`。
- 常量使用 `k` 前缀，例如 `kDefaultMainStreamFps`, `kAnalysisIntervalMs`。
- 局部 helper 多放在 `.cpp` 的匿名 namespace 中，例如 `mainwindow.cpp`, `rtspstream.cpp`。

## 目录组织习惯

- 项目源码主要平铺在根目录，`.cpp/.h/.ui/.qrc/.pro` 都在项目根。
- 资源按类型分目录：`icons/`, `images/`, `styles/`, `models/`。
- 构建输出在 `x64/Debug` 和 `x64/Release`，不应作为源码修改对象。

## 组件写法

- 基础 UI 用 `mainwindow.ui`，复杂动态区域在 `mainwindow.cpp` 里替换或插入控件。
- 样式通过对象名、动态属性和 QSS 控制，例如 `setRole()`、`repolish()`、`styles/iskating.qss`。
- 视频控件封装为 `VideoOpenGLWidget`，内部组合 `D3DVideoSurface`。

## API 写法

桌面端通过 `TrainingRepository`/QtNetwork 同步调用 FastAPI；普通业务路由使用 bearer JWT，worker 路由使用独立 token。C++ 内部接口通常使用：

- `bool initialize(..., QString *error)` 返回成功/失败和错误文本。
- `StatusCallback` / `ResultCallback` 从后台线程向 UI 线程发布状态或结果。
- `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 跨线程回调 UI。

相关文件：

- `tensorrtrunner.h`
- `trainingrepository.h/.cpp`
- `athleteanalysismanager.cpp`
- `server/app/main.py`

## 错误处理方式

- 可恢复错误通常设置状态文本并返回空结果，例如 `TensorRtAthleteBackend::infer()`。
- 致命视频错误通过 `RtspStream::setFatalError()` 停止重试。
- 用户输入错误使用 `QMessageBox::warning()`。
- 低层错误通过 `QString *error` 向上返回。

相关文件：

- `rtspstream.cpp`
- `systemsettingsdialog.cpp`
- `videoopenglwidget.cpp`
- `tensorrtrunner.cpp`

## 日志方式

- 使用 `qDebug()` 和 `qWarning()`。
- 记录 RTSP URL 前会用 `safeUrlForLog()` 遮蔽密码。

相关文件：

- `mainwindow.cpp`
- `videoopenglwidget.cpp`
- `rtspstream.cpp`

## 测试习惯

`server/tests` 使用 Python `unittest` 验证协议、schema 合同和纯逻辑，命令见 [[03-commands|运行命令]] 和 [[runbooks/testing|测试 Runbook]]。真实 PostgreSQL/API、Qt UI、GPU/DeepStream 和 12 路压测仍需集成环境。

## Git 工作规则

- 每次工作完成后，默认把本次变更提交到本地 Git 仓库，作为本地记录留存。
- 默认不推送远端/云端，不创建远端 PR；只有用户明确要求 `push`、发布或创建 PR 时才执行。
- 本地提交前应确认提交范围只包含本次任务相关文件，不要把已有无关未提交修改、生成文件或构建输出带入提交。
- 当前 GitHub 远端为 `https://github.com/EthanQing/iskating.git`，`main` 是默认且受保护的主分支。
- 所有改动必须在非 `main` 分支完成，并通过 Pull Request 合并到 `main`；禁止直接推送、强制推送或删除 `main`。
- 版本或重要基线使用带注释的 tag 标记；tag 应指向已经验证并合并的提交。

## 类型定义习惯

- 简单数据结构用 `struct` 放在头文件，例如 `AthleteAnalysisResult`, `TrainingSession`, `AnalysisTask`, `SharedCameraSettings`；`PoseFrameResult` 等旧姿态类型只用于兼容边界。
- 枚举使用 `enum class`，例如任务、视频或识别状态类型。
- Qt 容器与类型较多，例如 `QVector`, `QString`, `QImage`, `QPointF`。

## import/export 风格

- 头文件使用 include guard。
- `.cpp` 先包含自身头文件，再包含项目头和 Qt/系统头。
- qmake 的 `SOURCES`/`HEADERS` 需要手动维护，见 `mainwindow.pro`。

## 不应该做的事情

- 不要直接修改 `Makefile*`, `.qmake.stash`, `x64/`, `debug/`, `release/` 等生成文件。
- 不要绕过 `safeUrlForLog()` 打印带密码的 RTSP URL。
- 不要在 UI 线程里做长时间 TensorRT 构建或视频解码。
- 不要新增源码文件后忘记更新 `mainwindow.pro`。
- 不要假设 `.vscode/` 的 GCC 配置是当前真实构建方式；以 `mainwindow.pro` 为准。
