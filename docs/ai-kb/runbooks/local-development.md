# 本地开发 Runbook

[Runbook 地图](README.md) · [命令速查](../03-commands.md) · [环境变量](../references/environment-variables.md)

## 1. 前置条件

Windows 客户端：

- Visual Studio 2022 MSVC x64 / x64 Native Tools。
- Qt 6.7.3 MSVC 2022 x64，必须位于 `C:/Qt/6.7.3/msvc2022_64`。
- FFmpeg shared MSVC x64 开发包。
- TensorRT 10.1.0.27、CUDA 11.8、兼容 NVIDIA 驱动/GPU。
- PostgreSQL 和可运行 FastAPI 的 Python 环境。

DeepStream worker 只能在 Ubuntu/NVIDIA 环境完整开发验证，见 [部署 Runbook](deployment.md)。

## 2. 启动 PostgreSQL 与 FastAPI

先创建开发数据库和用户。然后：

```powershell
cd server
python -m venv .venv
.\.venv\Scripts\python -m pip install -r requirements.txt
$env:ISKATING_DATABASE_URL="postgresql+psycopg://iskating:password@127.0.0.1:5432/iskating"
$env:ISKATING_JWT_SECRET="dev-only-secret"
```

首次或可丢弃开发库执行：

```powershell
.\.venv\Scripts\python ..\tools\reset_postgres_schema.py --yes
```

该命令会删除业务表，不得用于需要保留的数据。启动服务：

```powershell
.\.venv\Scripts\uvicorn app.main:app --host 127.0.0.1 --port 8000
```

验证：

```powershell
Invoke-RestMethod http://127.0.0.1:8000/health
```

## 3. 准备模型

在仓库根目录：

```powershell
powershell -ExecutionPolicy Bypass -File tools/download_athlete_models.ps1
python tools/check_athlete_models.py
```

预期本机存在但不提交 Git：

- `models/athlete/yolo26x.onnx`
- `models/athlete/personvit_msmt17_vit_base.onnx`

首次客户端启动可能在同目录构建 `.fp16.engine`，耗时较长。

## 4. 构建客户端

打开 Visual Studio 2022 x64 Native Tools 终端，在仓库根目录：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
```

运行：

```powershell
.\x64\Release\iskating.exe
```

桌面端默认访问 `http://127.0.0.1:8000`，默认开发账号来自服务端 seed。可通过环境变量或 QSettings 覆盖。

## 5. 最小 smoke test

1. `/health` 返回 `status=ok`。
2. 客户端启动且不报 Qt platform/DLL 错误。
3. 系统设置可打开、保存并重新打开。
4. 人员列表能从服务端加载。
5. 至少一个 RTSP 或兼容本地文件可显示。
6. 模型状态符合预期：完整 AI、仅检测降级或明确不可用。

## 常见失败

| 现象 | 优先检查 |
|---|---|
| qmake 立即报错 | 是否使用固定 Qt qmake，而非 PATH 中其他版本 |
| 找不到 FFmpeg/TensorRT/CUDA | 对应 `*_ROOT` 和开发包 import lib/header |
| `nmake` 不存在 | 是否在 x64 Native Tools 环境 |
| FastAPI 导入失败 | `ISKATING_DATABASE_URL`、依赖安装、PostgreSQL 可达性 |
| FastAPI startup 失败 | schema 是否已创建、bcrypt 固定版本、seed 权限 |
| 视频有画面但无 AI | 模型状态、是否开始采集、活动流和 D3D→RGB 提取 |
| 首次启动很慢 | TensorRT engine 正在构建；看日志与 GPU 活动 |

## GitHub 协作

GitHub 远端为 `https://github.com/EthanQing/iskating.git`，`main` 是受保护的默认分支。所有改动都在非 `main` 分支完成，并通过 Pull Request 合并；不要直接向 `main` 推送。

首次迁移或重新配置本地远端：

```powershell
git remote set-url origin https://github.com/EthanQing/iskating.git
git fetch origin
```

开始一个改动：

```powershell
git switch main
git pull --ff-only origin main
git switch -c codex/<change>
```

完成验证后推送并创建 Pull Request：

```powershell
git push -u origin codex/<change>
gh pr create --base main --head codex/<change>
```

创建版本或重要基线 tag 时使用带注释 tag，并推送 tag：

```powershell
git tag -a v0.1.0 -m "Baseline release v0.1.0"
git push origin v0.1.0
```
