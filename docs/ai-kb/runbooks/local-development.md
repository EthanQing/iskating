# Local Development Runbook

上级入口：[[00-index|AI 知识库索引]]、[[runbooks/README|Runbook 地图]]
相关命令：[[03-commands|运行命令]]
相关 Reference：[[references/environment-variables|环境变量]]、[[references/third-party-services|第三方服务]]
常见风险：[[05-pitfalls|坑点]]、[[runbooks/debugging|调试 Runbook]]

## 本地启动步骤

1. 打开 Visual Studio 2022 x64 Native Tools 环境。
2. 确认 Qt 路径为 `C:/Qt/6.7.3/msvc2022_64`。
3. 在项目根目录运行 qmake。
4. 默认使用 `nmake release` 构建 Release 版本。
5. 默认运行 `x64/Release/iskating.exe`；只有需要调试符号和 Debug DLL 时才构建/运行 Debug。

示例：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
.\x64\Release\iskating.exe
```

Debug 示例：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=debug"
nmake debug
.\x64\Debug\iskating.exe
```

## 依赖安装

TODO: 仓库未提供自动安装脚本。根据 `mainwindow.pro`，本机需要：

- Qt 6.7.3 MSVC 2022 x64
- Visual Studio 2022 MSVC x64
- FFmpeg shared MSVC x64 dev package
- TensorRT 10.1.0.27
- CUDA 11.8

## 环境变量

可选覆盖：

- `FFMPEG_ROOT`
- `TENSORRT_ROOT`
- `CUDA_ROOT`
- `ISKATING_API_BASE_URL`
- `ISKATING_API_USERNAME`
- `ISKATING_API_PASSWORD`

Qt 根目录当前写在 `mainwindow.pro`，不是环境变量。

## 数据库和训练服务准备

桌面端启动前需要 PostgreSQL 和 FastAPI 训练服务：

```powershell
cd server
python -m venv .venv
.\.venv\Scripts\pip install -r requirements.txt
$env:ISKATING_DATABASE_URL="postgresql+psycopg://iskating:password@127.0.0.1:5432/iskating"
$env:ISKATING_JWT_SECRET="dev-secret"
python ..\tools\reset_postgres_schema.py --yes
.\.venv\Scripts\uvicorn app.main:app --host 127.0.0.1 --port 8000
```

桌面端默认连接 `http://127.0.0.1:8000`，也可用 `QSettings server/baseUrl` 或 `ISKATING_API_BASE_URL` 覆盖。

相关文件：

- `mainwindow.cpp`
- `main.cpp`

## 模型准备

- 运行 `tools/download_athlete_models.ps1` 下载 YOLO26x 和 TransReID MSMT17 checkpoint，并生成两个 ONNX 文件。
- 运行 `python tools/check_athlete_models.py` 检查模型文件和 SHA256。
- YOLO26x ONNX 输入为 `1x3x640x640`、输出为 `1x300x6`；PersonViT ONNX 输入为 `1x3x256x128`、输出为 `1x768`。
- 首次运行 TensorRT 可能生成 `.fp16.engine`。

## 常见启动失败原因

- qmake 不是 `C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe`。
- FFmpeg/TensorRT/CUDA 默认路径不存在，且没有设置覆盖环境变量。
- 输出目录缺少 Qt platforms 插件或 FFmpeg/TensorRT/CUDA DLL。
- GPU/驱动不支持当前 TensorRT/CUDA 或 D3D11VA 路径。
- 任一模型缺失时，视频播放仍可用；YOLO26x 缺失会停用 AI，PersonViT 缺失会停用身份匹配。

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
