# Open Questions

上级入口：[[00-index|AI 知识库索引]]
相关文档：[[01-project-overview|项目概览]]、[[02-architecture|架构说明]]、[[05-pitfalls|坑点]]
后续跟踪：[[tasks/backlog|后续建议]]、[[changelog/major-changes|主要变更]]
可能影响：[[runbooks/local-development|本地开发]]、[[runbooks/deployment|部署]]、[[references/third-party-services|第三方服务]]

## 是否存在正式产品需求或 README

当前观察：

- 项目根目录没有 `README.md`。
- 业务含义主要从 UI 文案和源码推断。

为什么不确定：

- 无法确认正式产品名称、目标用户、部署环境和发布标准。

建议后续确认：

- 补充 `README.md` 或产品说明，并同步更新 `01-project-overview.md`。

## 当前真实构建入口是否只有 qmake

当前观察：

- `mainwindow.pro` 是主要构建配置。
- 未找到 `CMakeLists.txt`。
- `.vscode/` 存在 GCC/GDB 配置，但与 `mainwindow.pro` 的 MSVC/Qt 强约束不一致。

为什么不确定：

- `.vscode/` 可能是旧配置或个人配置。

建议后续确认：

- 明确推荐构建命令，并考虑删除或更新过期编辑器配置。

## 测试策略是否存在于仓库外

当前观察：

- 未找到 `tests/`, `e2e/` 或测试命令。

为什么不确定：

- 旧项目可能依赖人工测试或外部测试设备。

建议后续确认：

- 记录最小人工验收流程，或补充可自动化的单元测试。

## 手部姿态后端是否仍计划接入主流程

当前观察：

- `TensorRtHandPoseBackend`、`handposeadapter.cpp` 和 `models/hand/` 存在。
- 当前 `HandAnalysisManager` 实际初始化的是 `TensorRtBodyPoseBackend`。

为什么不确定：

- 类名和实现存在历史演进痕迹，无法确认手部模块是否暂停、废弃或待接入。

建议后续确认：

- 如果手部识别是后续目标，应补充流程文档并说明与人体姿态结果如何合并。

## RTMW3D 模型和 engine 的版本管理策略

当前观察：

- `models/body/rtmw3d-x.onnx` 很大且被 `.gitignore` 忽略。
- `body_model.json` 仍引用该模型和 Hugging Face 下载地址。

为什么不确定：

- 无法确认发布包是否必须携带该模型，或允许运行时缺失。

建议后续确认：

- 明确模型分发方式、校验方式和缺失时的用户提示。

## 部署/发布流程是否有外部脚本

当前观察：

- 未找到 CI/CD、安装器或打包脚本。
- `mainwindow.pro` 只包含 post-link DLL/模型复制和 Release `windeployqt`。

为什么不确定：

- 可能有仓库外发布流程。

建议后续确认：

- 补充部署 runbook，包括目标机器驱动、VC Redistributable 和模型文件要求。
