# Initial Project Analysis Decisions

上级入口：[[00-index|AI 知识库索引]]、[[decisions/README|决策记录地图]]
相关文档：[[01-project-overview|项目概览]]、[[02-architecture|架构说明]]、[[05-pitfalls|坑点]]、[[07-open-questions|未确认问题]]
涉及模块：[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]、[[modules/background-workers|后台线程]]

## Decision: 使用 Qt Widgets + qmake 构建 Windows 桌面应用

### Status

Observed

### Context

项目以 `.pro`, `.ui`, `.qrc` 和 Qt Widgets 源码组织，没有 Web、CMake 或服务端入口。

### Evidence

- `mainwindow.pro`
- `mainwindow.ui`
- `main.cpp`
- `mainwindow.cpp`

### Consequences

好处是 UI 与 Windows 本地视频/Direct3D 能力结合紧密；代价是构建环境强依赖 Qt/MSVC/qmake，跨平台和自动化构建成本较高。

### Uncertainty

TODO: 未确认是否计划迁移到 CMake 或更新 Qt 版本。

## Decision: 强制使用官方 Qt 6.7.3 MSVC 2022 x64

### Status

Observed

### Context

`mainwindow.pro` 检查 `qmake.exe` 路径和当前 Qt prefix，不匹配时直接 `error()`。

### Evidence

- `mainwindow.pro`

### Consequences

好处是避免 ABI/插件路径混乱；代价是本机路径强绑定，协作者需要相同 SDK 或修改 `.pro`。

### Uncertainty

TODO: 未确认是否允许用环境变量覆盖 Qt 根目录。

## Decision: RTSP 视频链路采用 FFmpeg + D3D11VA 硬解

### Status

Observed

### Context

视频流要求低延迟，解码输出 D3D11 硬件帧，并直接交给 D3D surface 显示。

### Evidence

- `rtspstream.cpp`
- `d3d11videodevice.cpp`
- `d3dvideosurface.cpp`
- `mainwindow.pro`

### Consequences

好处是降低 CPU 拷贝和播放延迟；代价是依赖 Windows、GPU、D3D11VA 和支持硬解的编码器，没有通用软件解码 fallback。

### Uncertainty

TODO: 未确认目标摄像头编码格式和硬解兼容性范围。

## Decision: TensorRT FP16 engine 缓存在模型目录

### Status

Observed

### Context

`TensorRtRunner` 会从 ONNX 构建 `.fp16.engine` 并写回模型同目录，下次优先加载。

### Evidence

- `tensorrtrunner.cpp`
- `.gitignore`
- `models/body/body_model.json`
- `models/hand/hand_model.json`

### Consequences

好处是启动后续更快；代价是首次启动耗时，且 engine 与硬件/驱动/模型版本绑定，不适合提交或跨机器复用。

### Uncertainty

TODO: 未确认发布包是否预置 engine，还是由目标机器首次构建。

## Decision: 本地配置使用 QSettings，训练业务数据使用 SQLite

### Status

Superseded by SQLite schema v3

### Context

初始分析时未发现数据库或文件存储层，摄像头配置和训练历史均通过 `QSettings` 读写。当前实现已经把训练业务数据迁移到本地 SQLite：运动员、教练、动作标准、训练计划、训练 session、动作实例、人工复核、标准参考视频和个体基线都由 `TrainingRepository` 管理。`QSettings/trainingHistory` 仅作为旧数据迁移来源，不再写入新训练记录。

### Evidence

- `mainwindow.cpp`
- `mainwindow.h`
- `trainingrepository.cpp`
- `trainingdomain.h`
- `systemsettingsdialog.h`
- `main.cpp`

### Consequences

当前分层保留了 QSettings 对摄像头配置的轻量读写，同时用 SQLite 支撑本地产品版复盘校准、报告和趋势查询。代价是仍没有独立迁移工具、权限系统、备份恢复或云同步。

### Uncertainty

TODO: 未确认训练数据库的长期归档、备份恢复和隐私删除策略。

## Decision: 主流程以人体姿态为核心，手部模型暂未接入

### Status

Observed

### Context

仓库有手部模型后端和 adapter，但 `HandAnalysisManager` 实际初始化 `TensorRtBodyPoseBackend`。

### Evidence

- `handanalysismanager.cpp`
- `tensortrthandposebackend.cpp`
- `handposeadapter.cpp`
- `models/hand/hand_model.json`

### Consequences

好处是主流程聚焦人体 Body17/RTMW3D；代价是类名和文件名可能误导维护者。

### Uncertainty

TODO: 未确认手部能力是废弃、待接入还是实验遗留。
