# Testing Runbook

上级入口：[[00-index|AI 知识库索引]]、[[runbooks/README|Runbook 地图]]
相关命令：[[03-commands|运行命令]]
相关模块：[[modules/core|应用核心]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]
相关提示词：[[prompts/codex-review-code|代码审查提示词]]

## 单元测试

FastAPI 分块协议和 worker 编排使用 Python `unittest`：

```powershell
uv run --python 3.12 --with-requirements server/requirements.txt python -m unittest discover -s server/tests -p "test_*.py"
```

## 集成测试

当前自动测试覆盖 NAS URI 约束、gzip JSONL/SHA256、范围读取、原子提交和 worker 参数编排。数据库状态机需要在临时 PostgreSQL 上验证；DeepStream 原生 parser/管线需要在 Ubuntu + NVIDIA 环境构建和运行。

## E2E 测试

完整帧率 E2E 使用带已知帧号的 12 路视频，要求解码帧数等于结果帧数、`frameIndex` 从 0 连续、PTS 不倒退且无未声明缺口。随后进行 12 路 1080p60 一小时压力测试，并分别中断 worker、FastAPI 和 NAS，验证恢复后无重复/缺帧。

## 如何只跑某个测试

例如只运行分块测试：

```powershell
uv run --python 3.12 --with-requirements server/requirements.txt python -m unittest server.tests.test_analysis_artifacts
```

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
- 打开系统设置，点击“连通测试”：未配置 IP 应显示跳过；不可达 IP 应显示失败原因；可用 RTSP 应显示成功、UDP/TCP、分辨率和帧率；测试后表单内容不应被自动保存或改写。
- 导入本地视频训练并保存记录，打开“复盘校准”验证动作列表、片段定位、慢放、逐帧、关键帧定位和姿态叠加开关。
- 在复盘中手动新增动作、修正起止时间/有效性/分数/错误项/反馈，确认历史卡片、建议页趋势、报告和个体基线使用人工优先数据。
- 在动作标准编辑入口维护阈值、权重、目标次数/分数、提示文案和参考视频路径，确认重启后不被 seed 覆盖。
- 导出 Markdown、CSV 明细和 PDF 复盘报告，确认包含训练摘要、动作明细、AI 原始分、人工修正、教练备注和视频引用。
- 在历史页打开“报告中心”，导出 CSV/PDF，确认参与者、训练内时间筛选生效，轨迹/速度/关节角/角速度字段、单位及无数据提示一致。
- RTSP 源仍可预览、开始/暂停/停止采集、保存记录；RTSP 历史复盘应禁用精确 seek/慢放/逐帧并显示片段时间提示。
- RTSP 断流验证：正常播放后临时断开摄像头网络或关闭 RTSP 服务，确认 UI 显示断流重连；30 秒后显示长时间断流；恢复网络后自动回到播放状态，日志包含 `stream interrupted`、`reconnect scheduled` 和 `stream recovered`，且 URL 密码脱敏。
- 导入 12 路完整分析 manifest，确认媒体信息、人工时间校正、Windows NAS 根映射、每路进度/错误、取消、重试和版本号正确。
- 完成区间边分析边回放，随机 seek 到未完成和人为缺口区间，确认明确提示且不沿用上一帧检测框。
- 创建新模型运行，完成前仍显示旧激活版本；激活后缓存清空并读取新版本；清理视频后结果文件删除但审计状态保留。

相关文件：

- `mainwindow.cpp`
- `videoopenglwidget.cpp`
- `handanalysismanager.cpp`

## 完整帧率上线门槛

- 固定输入测试覆盖 YOLO parser、坐标缩放、PersonViT 预处理、gallery ambiguous 匹配和动态 batch 1/12/32。
- 12 路时间轴使用 NVR/摄像头时间戳；正常时间源下机位误差不超过一帧，人工校正形成新运行版本。
- 记录 GPU、CPU、显存、NAS 读写、YOLO 吞吐、ReID 调用率和每小时录像分析耗时，据此选择生产 GPU。
- 单路失败不得破坏其他源的已提交结果；异常恢复后分块索引无重复、frameIndex 无缺失。

## 常见测试失败原因

- 本机没有目标摄像头或 RTSP 地址不可访问。
- TensorRT 首次构建 engine 耗时过长，被误判为卡死。
- Release 和 Debug 的 DLL 部署状态不同。
- `Qt PrintSupport` 缺失时 PDF 导出或 Release 部署会失败；确认 `.pro` 包含 `printsupport` 且输出目录有 `Qt6PrintSupport.dll`。
- `QSettings` 保留了旧配置，导致复现结果不一致。
