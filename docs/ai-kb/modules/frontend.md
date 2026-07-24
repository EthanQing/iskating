# Qt Widgets 前端模块

上级入口：[[00-index|AI 知识库索引]]、[[modules/README|模块地图]]
相关模块：[[core|应用核心]]、[[video-streaming|视频流]]、[[persistence|本地持久化]]
相关流程：[[flows/main-user-flow|主用户流程]]、[[flows/video-streaming-flow|视频播放流程]]
相关约定：[[04-conventions|代码约定]]、[[05-pitfalls|坑点]]

## 作用

负责桌面界面布局、暗色仪表盘样式、视频占位/控制浮层、检测框/身份标签和历史/兼容复盘卡片展示。

## 关键文件

- `src/ui/mainwindow.ui`: 主界面布局，包含 `pages`, `mainImageLabel`, `cameraButton01` 到 `cameraButton12`。
- `resources/styles/iskating.qss`: 全局 QSS，按对象名和动态属性控制视觉。
- `resources/iskating.qrc`: 把 QSS、图片、SVG 图标加入 Qt 资源。
- `src/ui/iconutils.cpp`: SVG 图标着色和尺寸归一化。
- `src/ui/framelessdialog.cpp`: 自定义无边框对话框。
- `src/ui/systemsettingsdialog.cpp`: 系统设置对话框，包含公共 RTSP、12 路 IP、场地段标定、机位用途和采集偏好。
- `src/ui/videoopenglwidget.cpp`: 视频控件占位状态和浮层按钮。
- `src/ui/trainingreviewdialog.cpp`: 历史复盘校准对话框，包含主视频回放、动作列表、人工修正表单和标准参考视频。
- `src/ui/personmanagementdialog.cpp`: 人员管理对话框，维护运动员档案、教练档案和教练-运动员绑定关系。
- `src/ui/videoopenglwidget.cpp`: 视频检测框和身份标签覆盖层。

## 当前设计

界面基础来自 `src/ui/mainwindow.ui`，但多个区域在运行时动态装配：

- 实时采集页保留当前二维轨迹卡；服务端历史动作/评分记录仍可进入支持范围内的复盘，旧姿态关键点覆盖层已经从客户端移除。轨迹卡的展开/收起按钮目前尚未接线。
- `installMetricBars()` 动态插入分项评分进度条。
- 历史记录和建议卡片在 `refreshHistory()` / `refreshSuggestions()` 中动态生成。
- 历史卡片保留摘要、教练批注和导出入口；动作级复盘进入独立 `TrainingReviewDialog`，避免继续膨胀历史卡片。
- 训练趋势在建议页复用 `suggestionCard` 动态卡片样式，展示最近 7/30 天训练次数、人工优先均分、最佳分、动作完成数和弱项变化摘要。
- 训练上下文区提供“编辑标准”入口，可维护阈值、权重、目标次数/分数、提示文案和本地参考视频路径。
- 训练上下文区提供“人员管理”入口，可维护运动员档案、教练档案和带训关系；快速新增运动员/教练按钮仍保留用于训练现场录入。
- 训练上下文区提供训练备注输入，可记录主观感受、疲劳程度、冰面情况和训练重点；保存后历史卡片和报告会展示。
- 系统设置中的“场地与机位标定”表按 12 路相机维护是否参与轨迹、用途、覆盖起止距离、横向偏移、安装高度、朝向、俯仰和质量/兼容备注。
- 系统设置提供“连通测试”入口，按当前表单逐路探测 RTSP 预览流，并在临时结果表显示成功/失败、协议、分辨率、帧率和错误原因；测试结果不会自动保存。
- 复盘校准对话框左侧是主视频与标准参考视频，右侧是动作表格、播放控制和人工复核表单。点击动作行会定位到片段；本地视频支持 seek、慢放、逐帧和关键帧定位，RTSP 只打开源并显示片段时间提示。
- 报告导出从历史卡片触发，支持 Markdown、CSV 明细和 PDF 复盘报告，内容使用人工复核后的有效数据并保留 AI 原始分。
- 历史检索面板的“报告中心”按保存日期、参与者、训练内时间段和指标类别导出专项指标 CSV/PDF；没有保存专项指标的旧记录会明确显示无数据。
- 主视频标题栏运行时插入“导入视频”按钮，入口连接到 `MainWindow::importOfflineVideo()`，不直接改 `mainwindow.ui`。
- 左侧导航栏必须由 `mainwindow.ui` 中的 `QFrame#sidebar` 承载，折叠/展开只操作这个真实容器。
- 顶部图标按钮和右侧操作栏使用固定宽度，悬浮时只改变固定区域内的视觉状态，不通过 `setText()` 改变布局宽度。
- 小窗视频 overlay 会在空间不足时隐藏机位文本，只保留图标控制组，避免长机位名/IP 与播放按钮重叠。

## 对外接口

- 资源路径使用 Qt resource 形式，例如 `:/icons/start_cap.svg`。
- 动态样式通过 `setProperty()` 与 `repolish()` 刷新。
- 视频控件通过 `VideoOpenGLWidget` 的 public 方法设置播放源、占位文本和检测框/身份叠加。
- 复盘回放使用 `VideoOpenGLWidget::seekTo()`, `setPlaybackRate()`, `stepForward()`, `positionMs()`, `durationMs()`, `isSeekable()`；这些能力仅在本地文件路径上完整可用。
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

1. 把文件放入 `resources/icons/` 或 `resources/images/`。
2. 更新 `resources/iskating.qrc`。
3. 在代码中用 `:/icons/...` 或 `:/images/...` 引用。

### 修改系统设置字段

1. 更新 `systemsettingsdialog.h` 的 settings struct。
2. 更新 `systemsettingsdialog.cpp` 的 UI、getter、setter 和校验。
3. 更新 `src/ui/mainwindow.cpp` 的加载/保存逻辑。
4. 如果新增 QSettings key，同步更新 `references/database-schema.md`。

### 修改系统设置连通测试

1. UI 入口在 `SystemSettingsDialog::testCameraConnectivity()`。
2. 探测逻辑在 `CameraConnectivityTester`，不要用视频控件承载测试，避免影响正在播放的预览。
3. 结果弹窗只显示本次测试结果，不写回表单或 QSettings。

### 修改复盘校准界面

1. 优先修改 `src/ui/trainingreviewdialog.cpp/.h`，保持历史卡片只作为入口和摘要。
2. 人工复核保存必须通过 `TrainingRepository::saveRepetitionReview()` 或 `createManualRepetition()`，不要绕过仓储直接写 SQL。
3. 新增标准参考信息时通过 `TrainingRepository::saveActionStandard()` 保存，注意该方法会递增动作标准版本。
4. 回放控制应先判断 `VideoOpenGLWidget::isSeekable()`；RTSP/网络源需要保留清晰提示，不应假装支持精确定位。

### 修改人员管理界面

1. 优先修改 `src/ui/personmanagementdialog.cpp/.h`。
2. 人员档案必须通过 `TrainingRepository::saveAthleteProfile()` / `saveCoachProfile()` 保存，不要绕过仓储直接写 SQL。
3. 删除人员应走 `archiveAthlete()` / `archiveCoach()` 归档，保持历史训练记录可回看。
4. 新增源码文件后同步维护对应层的 `.pri`，并确认 `mainwindow.pro` 已包含该层。

## 注意事项

- 不要直接编辑生成的 `ui_mainwindow.h`；应改 `src/ui/mainwindow.ui` 或运行时装配代码。
- 修改 objectName 会影响 QSS 和 `MainWindow` 中的 `ui->xxx` 访问。
- 左侧栏折叠逻辑应操作 `ui->sidebar`，不要用 `ui->sidebarLayout->parentWidget()` 猜父控件。
- `VideoOpenGLWidget` 不是普通 QLabel，主视频和小窗都依赖其播放/状态逻辑。
- 动态文本（反馈、RTSP 地址、训练报告路径、动作明细）需要 word wrap 或 elide，不能让卡片/侧栏横向撑宽。

## 相关流程

- `../flows/main-user-flow.md`
- `../flows/video-streaming-flow.md`

## 未确认问题

- TODO: 未确认是否有设计稿或视觉规范文档。
