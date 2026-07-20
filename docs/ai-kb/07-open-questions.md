# Open Questions

上级入口：[[00-index|AI 知识库索引]]
相关文档：[[01-project-overview|项目概览]]、[[02-architecture|架构说明]]、[[05-pitfalls|坑点]]
后续跟踪：[[tasks/backlog|后续建议]]、[[changelog/major-changes|主要变更]]
可能影响：[[runbooks/local-development|本地开发]]、[[runbooks/deployment|部署]]、[[references/third-party-services|第三方服务]]

## 正式角色、权限和产品验收基线

当前观察：

- 项目已有 `README.md`、`docs/feature-inventory.md` 和完整分析用户指南。
- FastAPI 用户记录带 role，但业务路由没有基于 role 授权，客户端也没有可见登录/权限管理。

为什么不确定：

- 无法从仓库确认教练、运动员、运营和管理员的正式权限矩阵、生产验收指标和发布签字流程。

建议后续确认：

- 确认正式角色与授权矩阵，定义客户端登录/退出、token 过期和服务端 RBAC。
- 确认 Qt、FastAPI/PostgreSQL 与 DeepStream worker 的联合验收和生产发布标准。

## 当前真实构建入口是否只有 qmake

当前观察：

- `mainwindow.pro` 是主要构建配置。
- 未找到 `CMakeLists.txt`。
- `.vscode/` 存在 GCC/GDB 配置，但与 `mainwindow.pro` 的 MSVC/Qt 强约束不一致。

为什么不确定：

- `.vscode/` 可能是旧配置或个人配置。

建议后续确认：

- 明确推荐构建命令，并考虑删除或更新过期编辑器配置。

## 集成与 E2E 测试策略

当前观察：

- `server/tests` 已有 Python `unittest`，主要是纯函数和源码/schema 合同检查。
- 已有测试命令记录在 `03-commands.md` 和 `runbooks/testing.md`。

为什么不确定：

- 尚未用真实 PostgreSQL/API 验证 session/participant/轨迹外键闭环。
- 尚无自动 Qt UI/构建、GPU/DeepStream、12 路压力与故障恢复 E2E。

建议后续确认：

- 先补真实 PostgreSQL/API 集成测试，特别是主 participant UUID 与 `track_points`/`speed_metrics` 外键。
- 按测试 Runbook 实施 Qt、GPU 和 12 路已知帧号视频 E2E。

## 手部姿态后端是否仍计划接入主流程

当前观察：

- `TensorRtHandPoseBackend`、`handposeadapter.cpp` 和 `models/hand/` 存在。
- 当前实时主流程使用 `AthleteAnalysisManager` 和 `TensorRtAthleteBackend`；旧姿态后端仅作为历史兼容代码保留。

为什么不确定：

- 类名和实现存在历史演进痕迹，无法确认手部模块是否暂停、废弃或待接入。

建议后续确认：

- 如果手部识别是后续目标，应补充流程文档并说明与人体姿态结果如何合并。

## 模型、gallery 和 engine 的可复现策略

当前观察：

- `models/athlete` 的两个 ONNX 被 `.gitignore` 忽略，发布前需下载并通过 SHA256 校验。
- `athlete_models.json` 记录模型来源、版本、输入输出 shape 和运行时要求。
- 完整分析 run 虽保存模型、预处理和 `gallery_snapshot_hash` 字段，worker 实际使用固定模型配置并读取运行时当前 gallery。

为什么不确定：

- 二进制模型被 Git 忽略，发布机需要额外下载并校验；缺少模型时视频仍应可播放。
- 当前完整分析版本标签不足以复现当时模型和 gallery 内容。

建议后续确认：

- 保留 `tools/download_athlete_models.ps1` 和 `tools/check_athlete_models.py` 的下载/校验流程。
- 为完整分析设计不可变模型资产和 gallery 快照，并让 worker 按 run 加载。

## 主参与者轨迹/速度持久化

客户端在保存前为参与者预生成 UUID，但服务端会为主运动员重建 participant UUID；轨迹/速度仍引用客户端 UUID 时可被静默跳过。需确定 UUID 的单一权威生成位置，修复后用真实 PostgreSQL 集成测试锁定。

## 任务重启恢复与进度量纲

应用重启后可把服务端未完成任务显示为 `paused`，但不重建本地 job 参数，无法直接继续。完整 run 返回 0–1 进度，任务中心按百分比直接展示。需决定是持久化足以重建 job 的输入，还是明确只支持“重新发起”，并统一进度为 0–100 或 0–1。

## 完整分析的完整性合同

当前激活前检查分块 SHA256、块内范围/重叠和已登记帧总数，但没有强制全局 frameIndex 从 0 到末帧无 gap、PTS 跨块单调或 JSONL 内容与元数据一致。需定义产品级“完整”的精确上线门槛并补充 E2E。

## 部署/发布流程是否有外部脚本

当前观察：

- 未找到 CI/CD、安装器或打包脚本。
- `mainwindow.pro` 只包含 post-link DLL/模型复制和 Release `windeployqt`。

为什么不确定：

- 可能有仓库外发布流程。

建议后续确认：

- 补充部署 runbook，包括目标机器驱动、VC Redistributable 和模型文件要求。
