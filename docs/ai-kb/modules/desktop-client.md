# 桌面客户端模块

[模块地图](README.md) · [实时训练流程](../flows/realtime-training.md) · [Session 流程](../flows/session-and-review.md)

## 职责

Windows Qt 客户端负责：

- 应用启动、页面导航和训练上下文。
- 系统设置、摄像头模板、连通测试和本机存储设置。
- 12 路视频控件与主视图编排。
- Windows 实时 AI 和完整分析任务的 UI/线程编排。
- 人员、ReID 样本、比赛、动作标准、session、历史、复盘和报告交互。
- 通过 `TrainingRepository` 调用训练服务。

视频/AI 算法细节见 [视频与实时 AI](video-and-realtime-ai.md)。

## 分层与关键文件

### app

- `src/app/main.cpp`：在 `QApplication` 前配置 DLL/Qt 插件搜索路径，设置应用名/组织名并启动 `MainWindow`。

### ui

- `mainwindow.ui`：稳定基础布局。
- `mainwindow.cpp/.h`：集中式业务编排、采集状态、保存、历史、报告和建议。
- `videoopenglwidget.*`：视频控件与叠加层 UI。
- `systemsettingsdialog.*`：RTSP、12 路机位、四点标定、AI 策略、存储和连通测试。
- `personmanagementdialog.*`：运动员/教练/ReID 样本。
- `trainingreviewdialog.*`：旧动作数据复核和回放。
- `offlineanalysisdialog.*`、`analysistaskcenterdialog.*`：完整分析和任务中心。
- `trajectorywidget.*`：当前活跃的二维轨迹/速度展示，不是旧姿态组件。

### domain / application

- `trainingdomain.h`：API/训练领域 struct。
- `athleteanalysisresult.h`：实时检测、身份和 gallery 结构。
- `athleteanalysismanager.*`：实时 AI worker facade。
- `analysistaskmanager.*`：单并发分析任务队列。

### infrastructure

- `persistence/trainingrepository.*`：QtNetwork API 边界。
- `persistence/videostorageplan.*`：session 视频资产规范路径，不负责录像。
- `configuration/cameraconfigtemplate.*`：配置 JSON 导入导出。
- `configuration/cameraconnectivitytester.*`：独立 RTSP 探测，不影响播放控件。

## 状态与线程

- `MainWindow` 持有采集状态、当前机位、参与者、gallery、内存检测摘要和指标。
- `AthleteAnalysisManager` 在自己的线程执行模型初始化与推理，结果 queued 回 UI。
- `AnalysisTaskManager` 在独立线程中创建独立 Repository，按 FIFO 单并发执行。
- `TrainingRepository` API 是同步请求；新增调用必须避免在 UI 线程形成长时间批量阻塞。
- 应用重启后任务状态可恢复展示，但未重建 job 参数，`resumeTask()` 不能继续这些恢复任务。

## QSettings 边界

保存：

- `cameraDefaults/*`、`cameras/camera01..12/*`
- `capture/*`、`videoStorage/*`
- `server/baseUrl`、`auth/*`
- `offlineVideo/lastDir`、`offlineAnalysis/nasRoot`

训练业务数据不以 QSettings 为主存储。详细 key 见 [环境变量与本机配置](../references/environment-variables.md)。

## 常见修改联动

### 新增 UI 控件或交互

1. 判断是修改 `.ui` 还是运行时动态区域。
2. 检查 objectName/QSS/property 和固定尺寸约束。
3. 在 `setupConnections()` 或对应 dialog 内连接行为。
4. 如新增资源，更新 qrc；如新增源码，更新 `ui.pri`。
5. 验证主页面、窗口缩放、长文本和错误状态。

### 新增配置字段

1. 修改 `systemsettingsdialog.h` 对应 settings struct。
2. 修改 dialog 表单、getter/setter 和校验。
3. 修改 `MainWindow` QSettings 读取、保存和旧 key 兼容。
4. 若配置模板需要交换该字段，修改 `cameraconfigtemplate.*` 与客户端合同测试。
5. 更新 Reference。

### 修改训练字段

按 [训练服务模块](training-service.md) 的跨层清单同步领域 struct、Repository、API、schema、工具和测试。

## 约束

- 不让 QWidget 跨线程访问。
- 不直接写 PostgreSQL。
- 不将 `planned` 视频资产文案描述为已录制。
- 不重新接入已移除的旧姿态 overlay；未来恢复需新契约和开关。
- 历史和趋势必须区分新 person/ReID 零分记录与旧动作评分记录。
