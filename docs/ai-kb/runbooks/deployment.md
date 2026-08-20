# 部署 Runbook

[Runbook 地图](README.md) · [环境变量](../references/environment-variables.md) · [第三方依赖](../references/third-party-services.md)

> 仓库目前没有安装器、CI/CD 或一键生产部署。以下是代码支持的手工部署边界。

## 1. 训练服务

目标主机准备 PostgreSQL 和 Python 环境：

```powershell
cd server
python -m venv .venv
.\.venv\Scripts\python -m pip install -r requirements.txt
$env:ISKATING_DATABASE_URL="postgresql+psycopg://user:password@db-host:5432/iskating"
$env:ISKATING_JWT_SECRET="replace-with-a-long-random-secret"
$env:ISKATING_ADMIN_USER="admin"
$env:ISKATING_ADMIN_PASSWORD="replace-before-first-start"
$env:ISKATING_IDENTITY_GALLERY_ROOT="D:/iskating/identity-gallery"
```

空库/可丢弃库显式重建 schema；已有库按版本选择 backfill，不要盲目 reset。然后以正式进程管理器/反向代理运行 Uvicorn（仓库未提供具体配置）。

生产检查：

- `/health` 可达。
- 默认密码和 JWT secret 已替换。
- gallery root 可写且有备份策略。
- CORS、网络 ACL、TLS/反向代理符合现场要求。
- 数据库和文件目录备份恢复已演练。

## 2. Windows 客户端

在目标兼容构建机执行 Release：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
```

`build/qmake/deployment.pri` 会：

- 复制 FFmpeg、TensorRT、CUDA DLL。
- 复制平台插件并在 Release 调用 `windeployqt --no-translations`。
- 复制模型 manifest、SHA256、ROI，以及本机存在的 ONNX。
- 将输出放到 `x64/Release`。

交付前检查：

- `iskating.exe`、Qt DLL/`plugins/platforms/qwindows.dll`。
- FFmpeg/TensorRT/CUDA DLL 与目标驱动兼容。
- 两个 ONNX 已放入 `models/athlete` 并通过校验。
- Microsoft VC++ Redistributable 已安装或随交付提供。
- 不把 `obj/moc/rcc/ui`、临时日志或未验证跨机的 `.engine` 当作必要发布内容。
- 在干净目标机启动，验证训练服务连接、视频和 GPU。

模型准备：

```powershell
python tools/check_athlete_models.py
```

## 3. DeepStream worker

目标：Ubuntu 24.04、兼容 DeepStream 9 的 NVIDIA 驱动、Docker、NVIDIA Container Toolkit、可访问同一 NAS。

先在 Windows/模型工作机导出动态 batch 模型：

```powershell
powershell -ExecutionPolicy Bypass -File tools/export_deepstream_models.ps1
```

FastAPI 与 worker 配置相同长随机 worker token，并对 `nas://` 指向同一逻辑根。Ubuntu：

```bash
export ISKATING_API_BASE_URL=http://api-host:8000
export ISKATING_ANALYSIS_WORKER_TOKEN='replace-with-a-long-random-token'
export ISKATING_ANALYSIS_WORKER_ID=deepstream-01
export ISKATING_ANALYSIS_NAS_ROOT=/srv/iskating
docker compose -f analysis_worker/compose.yml config
docker compose -f analysis_worker/compose.yml up -d --build
```

检查：

- 容器可访问 GPU、API 和 `/mnt/iskating`。
- `models/athlete/deepstream` 完整且只读挂载。
- NAS 源可读、结果前缀可写，ACL 阻止修改无关目录。
- worker claim/heartbeat/chunk/finish 日志正常。
- Windows `offlineAnalysis/nasRoot` 能映射同一 `nas://` URI。

## 4. 上线门槛

- 执行 [测试 Runbook](testing.md) 中的平台测试和完整分析 E2E。
- 真实 PostgreSQL 验证 participant 与轨迹/速度外键；已知 UUID 缺口未修复前不得宣称 F-24/F-25 闭环完成。
- 12 路帧连续性、PTS、chunk 校验、中断恢复和至少 60 分钟压测完成。
- 明确监控、日志、备份、密钥轮换、回滚和故障联系人；这些目前不在仓库内。

## 5. 回滚

- 客户端：保留上一版完整 Release 目录；不要混用 DLL/模型/engine。
- 服务端：schema 变更前备份 PostgreSQL 和 gallery/NAS 索引；仓库没有自动 down migration。
- Worker：保留上一版镜像与匹配模型；切换前停止 claim 新 run，避免同一 run 使用混合实现。
