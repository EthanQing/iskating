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

## 构建验证

当前可用的本机 Release 构建命令：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && nmake release"
```

如果 PowerShell 中直接运行 `nmake` 提示找不到命令，需要先通过 Visual Studio `vcvars64.bat` 初始化 MSVC 环境。Release 构建会运行 `windeployqt` 并复制 Qt SQL driver、PrintSupport、FFmpeg/TensorRT/CUDA DLL 和模型目录。

## 建议的人工验证清单

在没有自动化测试前，修改后建议至少手动验证：

- 应用能启动到最大化主窗口。
- 系统设置可打开、保存和重新加载。
- 至少一路 RTSP 或本地视频源可播放。
- 开始、暂停、停止采集状态正确。
- 模型状态栏能显示初始化/运行状态。
- 姿态覆盖、骨架视图和轨迹视图不会崩溃。
- 保存训练记录后历史页和建议页刷新，并确认 PostgreSQL 中生成对应 session 和动作实例。
- 用旧 SQLite 样本库执行 `tools/import_sqlite_to_postgres.py`，确认导入数量、历史记录、旧动作复盘和关键帧姿态 JSON 兼容。
- 打开“人员管理”，新增/编辑/删除运动员和教练，确认删除后训练下拉不再显示该人员但历史记录仍可展示。
- 导入本地视频训练并保存记录，打开“复盘校准”验证动作列表、片段定位、慢放、逐帧、关键帧定位和姿态叠加开关。
- 在复盘中手动新增动作、修正起止时间/有效性/分数/错误项/反馈，确认历史卡片、建议页趋势、报告和个体基线使用人工优先数据。
- 在动作标准编辑入口维护阈值、权重、目标次数/分数、提示文案和参考视频路径，确认重启后不被 seed 覆盖。
- 导出 Markdown、CSV 明细和 PDF 复盘报告，确认包含训练摘要、动作明细、AI 原始分、人工修正、教练备注和视频引用。
- RTSP 源仍可预览、开始/暂停/停止采集、保存记录；RTSP 历史复盘应禁用精确 seek/慢放/逐帧并显示片段时间提示。

相关文件：

- `mainwindow.cpp`
- `videoopenglwidget.cpp`
- `handanalysismanager.cpp`

## 常见测试失败原因

- 本机没有目标摄像头或 RTSP 地址不可访问。
- TensorRT 首次构建 engine 耗时过长，被误判为卡死。
- Release 和 Debug 的 DLL 部署状态不同。
- `Qt PrintSupport` 缺失时 PDF 导出或 Release 部署会失败；确认 `.pro` 包含 `printsupport` 且输出目录有 `Qt6PrintSupport.dll`。
- `QSettings` 保留了旧配置，导致复现结果不一致。
