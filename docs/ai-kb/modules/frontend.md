# Qt Widgets 前端模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[core|应用核心]]、[[video-streaming|视频流]]、[[pose-analysis|姿态分析]]
相关流程：[[flows/main-user-flow|主用户流程]]、[[flows/video-streaming-flow|视频播放流程]]
相关约定：[[04-conventions|代码约定]]、[[05-pitfalls|坑点]]

## 作用

负责桌面界面布局、暗色仪表盘样式、视频占位/控制浮层、骨架/轨迹可视化和历史/建议卡片展示。

## 关键文件

- `mainwindow.ui`: 主界面布局，包含 `pages`, `mainImageLabel`, `cameraButton01` 到 `cameraButton12`。
- `styles/iskating.qss`: 全局 QSS，按对象名和动态属性控制视觉。
- `iskating.qrc`: 把 QSS、图片、SVG 图标加入 Qt 资源。
- `iconutils.cpp`: SVG 图标着色和尺寸归一化。
- `framelessdialog.cpp`: 自定义无边框对话框。
- `systemsettingsdialog.cpp`: 系统设置对话框。
- `videoopenglwidget.cpp`: 视频控件占位状态和浮层按钮。
- `skeletonviewwidget.cpp`: 2D/3D 骨架绘制。
- `trajectorywidget.cpp`: 三维轨迹绘制。

## 当前设计

界面基础来自 `mainwindow.ui`，但多个区域在运行时动态装配：

- `installSkeletonView()` 用 `SkeletonViewWidget` 替换原 pose 占位控件。
- `installTrajectoryWidget()` 用 `TrajectoryWidget` 替换原轨迹占位控件。
- `installMetricBars()` 动态插入分项评分进度条。
- 历史记录和建议卡片在 `refreshHistory()` / `refreshSuggestions()` 中动态生成。
- 训练趋势 v1 在建议页复用 `suggestionCard` 动态卡片样式，展示最近 7/30 天训练次数、平均分、最佳分、动作完成数和弱项变化摘要。
- 主视频标题栏运行时插入“导入视频”按钮，入口连接到 `MainWindow::importOfflineVideo()`，不直接改 `mainwindow.ui`。
- 左侧导航栏必须由 `mainwindow.ui` 中的 `QFrame#sidebar` 承载，折叠/展开只操作这个真实容器。
- 顶部图标按钮和右侧操作栏使用固定宽度，悬浮时只改变固定区域内的视觉状态，不通过 `setText()` 改变布局宽度。
- 小窗视频 overlay 会在空间不足时隐藏机位文本，只保留图标控制组，避免长机位名/IP 与播放按钮重叠。

## 对外接口

- 资源路径使用 Qt resource 形式，例如 `:/icons/start_cap.svg`。
- 动态样式通过 `setProperty()` 与 `repolish()` 刷新。
- 视频控件通过 `VideoOpenGLWidget` 的 public 方法设置播放源、占位文本和姿态叠加。
- 离线视频导入按钮使用 `:/icons/video.svg` 和固定尺寸 `secondaryButton` 样式，避免挤压主视频标题栏。

## 常见修改任务

### 调整界面样式

1. 优先修改 `styles/iskating.qss`。
2. 如果需要动态状态，先确认控件是否已有 objectName 或 property。
3. 修改后启动应用检查主页面、历史页和建议页。

### 调整图标按钮或悬浮提示

1. 不要在 `Enter/Leave` 中通过改变按钮文本来扩展布局。
2. 对顶部按钮、右侧操作栏按钮和历史卡片按钮使用固定宽高与固定 `QSizePolicy`。
3. 悬浮说明优先通过固定宽度内文字、tooltip 或 statusTip 表达，确保主布局不横向跳动。

### 新增资源

1. 把文件放入 `icons/` 或 `images/`。
2. 更新 `iskating.qrc`。
3. 在代码中用 `:/icons/...` 或 `:/images/...` 引用。

### 修改系统设置字段

1. 更新 `systemsettingsdialog.h` 的 settings struct。
2. 更新 `systemsettingsdialog.cpp` 的 UI、getter、setter 和校验。
3. 更新 `mainwindow.cpp` 的加载/保存逻辑。

## 注意事项

- 不要直接编辑生成的 `ui_mainwindow.h`；应改 `mainwindow.ui` 或运行时装配代码。
- 修改 objectName 会影响 QSS 和 `MainWindow` 中的 `ui->xxx` 访问。
- 左侧栏折叠逻辑应操作 `ui->sidebar`，不要用 `ui->sidebarLayout->parentWidget()` 猜父控件。
- `VideoOpenGLWidget` 不是普通 QLabel，主视频和小窗都依赖其播放/状态逻辑。
- 动态文本（反馈、RTSP 地址、训练报告路径、动作明细）需要 word wrap 或 elide，不能让卡片/侧栏横向撑宽。

## 相关流程

- `../flows/main-user-flow.md`
- `../flows/video-streaming-flow.md`

## 未确认问题

- TODO: 未确认是否有设计稿或视觉规范文档。
