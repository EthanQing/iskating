# Testing Runbook

上级入口：[[00-index|AI 知识库索引]]、[[runbooks/README|Runbook 地图]]
相关命令：[[03-commands|运行命令]]
相关模块：[[modules/core|应用核心]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]
相关提示词：[[prompts/codex-review-code|代码审查提示词]]

## 单元测试

TODO: 未在项目中找到单元测试框架或命令。

## 集成测试

TODO: 未在项目中找到集成测试目录或命令。

## E2E 测试

TODO: 未在项目中找到 E2E 测试目录或命令。

## 如何只跑某个测试

TODO: 当前没有可确认的测试命令。

## 建议的人工验证清单

在没有自动化测试前，修改后建议至少手动验证：

- 应用能启动到最大化主窗口。
- 系统设置可打开、保存和重新加载。
- 至少一路 RTSP 或本地视频源可播放。
- 开始、暂停、停止采集状态正确。
- 模型状态栏能显示初始化/运行状态。
- 姿态覆盖、骨架视图和轨迹视图不会崩溃。
- 保存训练记录后历史页和建议页刷新。

相关文件：

- `mainwindow.cpp`
- `videoopenglwidget.cpp`
- `handanalysismanager.cpp`

## 常见测试失败原因

- 本机没有目标摄像头或 RTSP 地址不可访问。
- TensorRT 首次构建 engine 耗时过长，被误判为卡死。
- Release 和 Debug 的 DLL 部署状态不同。
- `QSettings` 保留了旧配置，导致复现结果不一致。
