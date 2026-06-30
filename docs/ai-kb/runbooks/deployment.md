# Deployment Runbook

上级入口：[[00-index|AI 知识库索引]]、[[runbooks/README|Runbook 地图]]
相关命令：[[03-commands|运行命令]]
相关 Reference：[[references/environment-variables|环境变量]]、[[references/third-party-services|第三方服务]]
相关问题：[[07-open-questions|未确认问题]]、[[05-pitfalls|坑点]]

## 构建步骤

建议在 x64 MSVC 环境构建 Release：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
```

输出目录：

- `x64/Release/iskating.exe`

## 部署相关配置

`mainwindow.pro` 的 post-link 规则会：

- 复制 FFmpeg DLL 到 `DESTDIR`
- 复制 TensorRT/CUDA DLL 到 `DESTDIR`
- 复制 `models/` 到 `DESTDIR/models`
- Release 下调用 `windeployqt.exe --release --no-translations`
- 训练业务数据不再随桌面端本地创建，需要先部署 FastAPI 服务和 PostgreSQL。

相关文件：

- `mainwindow.pro`

## CI/CD 线索

TODO: 未找到 `.github/workflows/`、其他 CI 配置、安装器脚本或发布脚本。

## 部署前检查

- 确认 PostgreSQL 数据库已创建，`alembic upgrade head` 已执行。
- 确认 FastAPI 服务可通过内网访问，例如 `http://训练服务器:8000/health`。
- 确认桌面端 `server/baseUrl` 指向训练服务，或设置 `ISKATING_API_BASE_URL`。
- 如需迁移旧数据，先执行 `tools/import_sqlite_to_postgres.py` 并核对导入数量。
- 确认 `x64/Release` 中存在 `iskating.exe`。
- 确认 `platforms/qwindows.dll` 等 Qt 插件已部署。
- 确认 FFmpeg DLL 存在，例如 `avcodec-62.dll`, `avformat-62.dll`。
- 确认 TensorRT/CUDA DLL 存在，例如 `nvinfer_10.dll`, `cudart64_110.dll`。
- 确认 `models/body` 和 `models/hand` 已复制。
- 确认目标机器 GPU/驱动支持 TensorRT/CUDA/D3D11VA。
- 确认 VC Redistributable 需求。

## 无法确认的信息

- TODO: 是否需要安装器。
- TODO: 是否需要预构建 TensorRT engine。
- TODO: 是否需要将 `rtmw3d-x.onnx` 放入发布包。
- TODO: 目标机器最低硬件和驱动版本。
