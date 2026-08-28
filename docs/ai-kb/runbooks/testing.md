# 测试与验收 Runbook

[Runbook 地图](README.md) · [命令速查](../03-commands.md) · [已知风险](../05-pitfalls.md)

## 自动测试

### Python 服务与 worker 合同

```powershell
python -m unittest discover -s server/tests -p "test_*.py"
```

覆盖重点：NAS URI/分块、模型合同、worker 参数、分析任务 schema、轨迹/速度/关节 schema 和相机连通性合同。多数测试不启动真实 PostgreSQL、FastAPI server 或 DeepStream。

### 客户端合同测试

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" tests/client/client-tests.pro "CONFIG+=release"
nmake release
.\x64\Release\tests\client-tests.exe
```

覆盖纯逻辑合同，例如摄像头模板和视频资产规划；不覆盖完整 UI、RTSP、GPU 或 API。

### 客户端构建

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
```

## 按变更选择最小验证

| 变更 | 最小自动验证 | 还需人工/集成验证 |
|---|---|---|
| QSS/Qt UI | 客户端构建 | 页面、缩放、长文本、交互状态 |
| 摄像头配置模板 | 客户端合同测试 | 导入预览、保存、旧 QSettings 兼容 |
| RTSP/视频 | 客户端构建 | UDP/TCP、断流恢复、D3D11VA、seek、stop |
| YOLO/ReID/track | 客户端构建 + 模型校验 | GPU 输入输出、多人、TTL、gallery、降级 |
| FastAPI 纯逻辑 | 相关 Python test | 真实 HTTP/JWT（如涉及） |
| schema/外键 | Python schema test | 临时 PostgreSQL reset + API 集成 |
| worker Python | worker/artifact/model tests | compose config、取消/heartbeat |
| DeepStream C++/parser | 相关 Python contract | Ubuntu DeepStream GPU 固定输入 E2E |
| 文档 | 链接/路径检查 | 命令与当前代码人工核对 |

## 必做数据集成用例

涉及 session、participant、轨迹或速度时，在可丢弃 PostgreSQL 上验证：

1. 创建主运动员和至少一名附加 participant。
2. POST session，包含各自 participantId 的 track point/speed。
3. 查询 `training_session_participants`、`track_points`、`speed_metrics`。
4. 断言每条 participant 外键存在且数量正确。
5. GET session 指标 API，断言 participant、时间、camera 和单位映射正确。

当前主 participant UUID 缺口可能让该用例失败；修复前不得将其标为通过。

## Windows 人工验收

按任务选择：

- 启动、系统设置保存/重载、人员和标准加载。
- RTSP 主/预览、小窗切换、UDP→TCP、30 秒断流和恢复，日志 URL 脱敏。
- 本地视频导入失败不改变当前源；成功后可 seek/慢放/逐帧。
- 开始/暂停/停止后计时、叠加、轨迹和内存结果状态一致。
- YOLO 缺失时视频继续；PersonViT 缺失时保留 unknown bbox。
- 多人 track 不在同帧复用；gallery 切换清理旧身份。
- 未标定/unknown 无轨迹；有效标定输出米制坐标和 `m/s`。
- 保存后历史刷新；新 person/ReID session 不新增虚假动作/评分/姿态。
- RTSP `planned` 与本地 `external` 视频资产文案正确。

## 完整分析 E2E 门槛

在 Ubuntu/NVIDIA/真实 NAS 或等效环境：

1. 使用 12 路带可见 frameIndex 的固定视频。
2. 核对每路解码帧数、结果帧数、frameIndex、PTS 和 batchTime。
3. 校验 chunk SHA256、范围、无重叠、无未声明 gap。
4. 在未完成/缺口区间回放，确认不沿用旧 bbox。
5. 中断 worker、FastAPI、NAS，验证租约、幂等登记和恢复。
6. 做 12 路 1080p60 至少 60 分钟压测，记录 GPU/CPU/显存/NAS I/O/吞吐。

当前代码尚未把全局 frameIndex/PTS/内容一致性全部作为激活硬门槛，验收需额外检查。

## 报告测试结果

只报告实际执行的命令、退出结果和环境。无法验证真实摄像头、GPU、PostgreSQL 或 DeepStream 时，明确列为未验证，不用单元测试替代平台结论。
