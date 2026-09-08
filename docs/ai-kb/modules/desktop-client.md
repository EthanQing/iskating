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

- `mainwindow.ui`：应用侧栏、上下文页头、实时训练和历史页面的基础控件；`MainWindow` 装配动态表单、工作区分隔和训练设置面板。
- `mainwindow.cpp/.h`：集中式业务编排、采集状态、保存、历史、报告和建议。
- `animatedbutton.*`：按钮背景、描边、文字/图标颜色的轻量动画，以及侧栏文字淡出；沿用 QPushButton 的点击和键盘语义。
- `videoopenglwidget.*`：视频控件与叠加层 UI；视频源模式支持单击、键盘选择、选中状态及播放控制菜单。
- `resources/styles/iskating.qss`：全局语义颜色、输入控件、菜单、对话框、滚动条和状态样式；动画按钮的过渡由 C++ 绘制。
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

## 实时训练界面

- 侧栏展开/折叠宽度为 220/64px，宽度和文字透明度同时过渡；折叠图标居中并保留 ToolTip。一级导航为实时训练、历史复盘，旧建议代码不再作为一级页面暴露。
- 主视频继续使用真实 `VideoOpenGLWidget` 和 D3D surface，来源以原生同级标签叠层显示；本地播放与 RTSP LIVE 文案区分。Camera 底栏在真实播放时仍显示机位、中文状态和选中反馈。
- 12 路视频源通过 `QGridLayout` 按可用宽度切换六列或四列，不使用横向滚动带；二维轨迹独立位于下方。
- 视频区与 Inspector 通过 `QSplitter` 分配宽度，Inspector 默认约 340px、最小 300px。运动员、实时状态、AI 状态分区展示，训练生命周期操作固定在下方。
- 训练设置使用独立的非模态 `FramelessDialog` 面板，在视频区右侧打开，不挤压 Inspector；运动员选择与 Inspector 同步，其余训练上下文、模型精度和分析 FPS 在面板中纵向滚动。
- 正常界面只显示简短 AI 可用状态，详细模型信息放在 ToolTip；未取得数据的状态显示 `—`。
- 轨迹空状态由真实 `TrajectoryWidget` 绘制；展开入口使用带运动员和时间范围的轨迹详情窗口。
- 不再在实时页面展示旧评分或历史汇总卡片；动作标准和目标字段保留为训练保存协议的兼容元数据。
- D3D 视频使用原生子窗口，页面切换不能直接对其父容器应用透明度效果；当前过渡使用独立覆盖层。

## 历史复盘界面

- 常用筛选与教练、比赛、场次分层排列，动作标准及历史评分放在更多筛选中；查询、排序和分页继续调用原 Repository 查询。
- 汇总按当前页记录分类展示，不把旧动作评分作为所有当前训练的通用 KPI。
- 记录默认显示摘要和操作，详细上下文、旧动作信息及片段定位按需展开；无记录时空状态位于结果容器中央。

## 系统设置界面

- `SystemSettingsDialog` 继续沿用 `FramelessDialog`，默认 1100×760、最小 900×640，初始高度受屏幕可用区域约束，并提供尺寸调节手柄。
- 左侧分类导航切换右侧 `QStackedWidget`：视频与摄像头、场地与轨迹、AI 分析、存储；各页独立滚动，取消和保存设置固定在底部。
- 公共 RTSP 使用双列表单，12 路 IP 使用三组四行；模板导入/导出和连通测试保留在视频页工具栏。
- 场地配置表及四点标定独立放在场地页。AI 和存储页复用原配置对象、校验及回调，不改变 QSettings 或配置模板契约。

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
