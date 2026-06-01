# 主用户流程

上级入口：[[00-index|AI 知识库索引]]、[[flows/README|流程地图]]
相关模块：[[modules/core|应用核心]]、[[modules/frontend|Qt Widgets 前端]]、[[modules/persistence|本地持久化]]、[[modules/pose-analysis|姿态分析]]
后续流程：[[video-streaming-flow|视频播放流程]]、[[pose-analysis-flow|姿态分析流程]]、[[training-record-flow|训练记录流程]]
相关 Runbook：[[runbooks/local-development|本地开发]]、[[runbooks/debugging|调试]]

## 简介

用户配置 12 路摄像头和场地覆盖段后启动采集，系统显示多路预览和主视图，对参与轨迹的相机流做姿态分析并拼接全场轨迹，最后保存训练记录并查看历史和建议。

## 触发条件

- 用户启动应用。
- 用户点击系统设置、开始采集、暂停、停止、保存记录或页面导航按钮。

## 流程步骤

1. `main.cpp` 创建 `QApplication`，设置本地插件/运行库路径，显示最大化 `MainWindow`。
2. `MainWindow` 构造时初始化 UI、加载摄像头配置、加载训练历史、创建 AI 分析管理器。
3. 用户打开系统设置，`SystemSettingsDialog` 收集公共 RTSP 参数、12 路 IP、每路是否参与轨迹和场地起止距离。
4. 点击开始采集后，主视图默认显示第一路主码流，12 路小窗显示预览码流。
5. AI 分析器订阅参与轨迹的相机活动流，持续输出带 `cameraId` 的姿态结果。
6. `MainWindow` 刷新选中机位视频覆盖、骨架、全场轨迹、动作次数和评分。
7. 用户点击保存记录后，当前训练数据通过 `TrainingRepository` 写入本机 SQLite，旧 `QSettings/trainingHistory` 仅作为首次迁移来源。
8. 历史页展示最近训练记录、复盘校准与报告导出入口，建议页基于最近记录、动作标准和 7/30 天趋势生成建议。

## 涉及文件

- `main.cpp`
- `mainwindow.cpp`
- `mainwindow.ui`
- `systemsettingsdialog.cpp`
- `videoopenglwidget.cpp`
- `handanalysismanager.cpp`
- `posestandardnessscorer.cpp`

## 涉及数据

- `SharedCameraSettings`
- `CameraSlotSettings`
- `CapturePreferenceSettings`
- `TrainingSession`
- `ActionRepetition`
- `SessionHistoryItem`
- `PoseFrameResult`
- `QSettings` keys: `cameraDefaults/*`, `cameras/cameraXX/*`, `capture/*`
- SQLite tables: `training_sessions`, `action_repetitions`, `action_standards`

## 错误处理

- 设置对话框校验端口和预览路径，失败时 `QMessageBox::warning()`。
- 视频源为空或文件不存在时在视频控件显示状态文本。
- AI 初始化失败时模型状态栏显示错误。

## 边界情况

- 没有配置摄像头 IP 时，主视图显示未配置。
- RTSP 采集开始前至少需要一路参与轨迹的相机配置 IP 和有效覆盖段；离线视频不需要相机 IP。
- 暂停后清空实时姿态，但训练历史不受影响。
- 训练时长为 0 时保存记录会提示“暂无可保存的训练记录”。
- RTMW3D 缺失时仍可使用 2D 人体姿态。
