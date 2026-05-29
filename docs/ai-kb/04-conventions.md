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

本项目没有 HTTP API。内部接口通常使用：

- `bool initialize(..., QString *error)` 返回成功/失败和错误文本。
- `StatusCallback` / `ResultCallback` 从后台线程向 UI 线程发布状态或结果。
- `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 跨线程回调 UI。

相关文件：

- `tensorrtrunner.h`
- `tensorrtbodyposebackend.h`
- `handanalysismanager.cpp`

## 错误处理方式

- 可恢复错误通常设置状态文本并返回空结果，例如 `TensorRtBodyPoseBackend::infer()`。
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

TODO: 当前项目未发现测试文件、测试框架或测试命令。

## 类型定义习惯

- 简单数据结构用 `struct` 放在头文件，例如 `PoseFrameResult`, `TrainingRecord`, `SharedCameraSettings`。
- 枚举使用 `enum class`，例如 `PoseSkeletonType`, `PoseInstanceKind`, `Handedness`。
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
