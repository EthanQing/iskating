# 应用核心模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[frontend|Qt Widgets 前端]]、[[persistence|本地持久化]]、[[background-workers|后台线程]]
相关流程：[[flows/main-user-flow|主用户流程]]、[[flows/pose-analysis-flow|运动员检测与身份流程]]、[[flows/training-record-flow|训练记录流程]]
相关约定：[[04-conventions|代码约定]]、[[05-pitfalls|坑点]]

## 作用

核心模块负责启动应用、装配主窗口、协调摄像头、AI 分析、训练统计、历史记录和页面导航。

## 关键文件

- `src/app/main.cpp`: 配置本地 DLL/Qt 插件搜索路径，创建 `QApplication` 和 `MainWindow`。
- `src/ui/mainwindow.h`: 定义主窗口状态、训练上下文控件、训练记录加载入口和主要私有方法。
- `src/ui/mainwindow.cpp`: 主窗口业务编排、页面切换、采集控制、人员管理入口、训练记录保存、复盘/报告入口和评分刷新。
- `src/ui/mainwindow.ui`: Qt Designer 生成的基础界面布局。
- `src/ui/personmanagementdialog.cpp`: 主窗口训练上下文中打开的人员管理对话框。

## 当前设计

`MainWindow` 是集中式编排对象：

- 构造时创建 `AthleteAnalysisManager`。
- 将 AI 回调结果同步到主视频检测框、身份标签和训练记录字段。
- 按页面维护实时采集、历史分析、纠正建议三个主页面。
- 训练上下文区通过 `openPersonManagement()` 打开人员管理对话框，保存后重新加载人员下拉、历史和建议。
- 使用 `QTimer` 每秒累计训练时长。
- 使用成员变量保存当前机位、采集状态、分数、最近训练 session 摘要和 UI 控件集合；训练业务数据通过 `TrainingRepository` 调用 FastAPI/PostgreSQL 服务。

## 对外接口

该模块没有稳定公共 API。其他代码主要通过 Qt 信号/回调与 `MainWindow` 交互：

- `AthleteAnalysisManager::setResultCallback()` 将运动员检测结果回调给 `MainWindow`。
- `VideoOpenGLWidget::setStreamChangedHandler()` 通知主视图活动流变化。
- UI 按钮通过 `setupConnections()` 绑定到私有方法。

## 常见修改任务

### 新增主窗口按钮行为

1. 在 `src/ui/mainwindow.ui` 添加或确认控件。
2. 在 `MainWindow::setupConnections()` 绑定信号。
3. 若需要样式，在 `resources/styles/iskating.qss` 添加对象名或动态属性规则。

### 修改训练保存字段

1. 更新 `src/domain/trainingdomain.h` 中对应的 `TrainingSession`、`ActionRepetition` 或 `SessionHistoryItem`。
2. 同步更新 `src/infrastructure/persistence/trainingrepository.cpp` 的保存和读取逻辑。
3. 更新 `src/ui/mainwindow.cpp` 中的保存、`loadTrainingRecords()`、`refreshHistory()`、`refreshSuggestions()` 和报告/复盘展示逻辑。

### 修改采集状态逻辑

1. 阅读 `startCapture()`, `pauseCapture()`, `stopCapture()`。
2. 确认 `m_isRecording`, `m_isPaused`, `m_timer`, `AthleteAnalysisManager` 的状态同步。
3. 手动验证开始、暂停、停止、保存记录。

## 注意事项

- 不要把耗时推理或视频解码放到 UI 线程。
- 不要绕开 `clearRealtimeAnalysisFrame()` 手工清检测覆盖层，避免状态不一致；该函数只清当前 `AthleteFrameResult`。
- 修改摄像头配置时要兼容旧 QSettings 字段。
- 新增源码文件需要更新对应层的 `.pri`，并确认 `mainwindow.pro` 已包含该层。

## 相关流程

- `../flows/main-user-flow.md`
- `../flows/pose-analysis-flow.md`
- `../flows/training-record-flow.md`

## 未确认问题

- TODO: 未确认是否存在正式的产品需求文档。
- TODO: 未确认 UI 页面是否还会继续通过 Qt Designer 维护。
