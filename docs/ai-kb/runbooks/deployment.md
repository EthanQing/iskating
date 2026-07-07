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

## Release 发布目录清单

默认可交付目录为 `x64/Release`。完成 Release 构建后，交付包至少应包含：

- `iskating.exe`
- Qt 运行时 DLL：至少包括 `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, `Qt6Network.dll`, `Qt6PrintSupport.dll`
- Qt 插件目录：至少包括 `plugins/platforms/qwindows.dll`，以及 `windeployqt` 复制出的 `imageformats/`, `iconengines/`, `networkinformation/`, `styles/`, `tls/` 等运行时目录
- FFmpeg DLL：`avcodec-62.dll`, `avformat-62.dll`, `avutil-60.dll`, `swresample-6.dll`, `swscale-9.dll`
- TensorRT DLL：`nvinfer_10.dll`, `nvinfer_plugin_10.dll`, `nvinfer_vc_plugin_10.dll`, `nvinfer_dispatch_10.dll`, `nvinfer_lean_10.dll`, `nvonnxparser_10.dll`, `nvinfer_builder_resource_10.dll`
- CUDA DLL：`cudart64_110.dll`, `cublas64_11.dll`, `cublasLt64_11.dll`, `cufft64_10.dll`, `cufftw64_10.dll`, `curand64_10.dll`, `cusolver64_11.dll`, `cusolverMg64_11.dll`, `cusparse64_11.dll`, `nvrtc64_112_0.dll`, `nvrtc-builtins64_118.dll`
- `models/`：至少包括 `models/body` 与 `models/hand`；`models/body/rtmw3d-x.onnx` 如果未随源码仓库提供，需要在发布前补齐或明确 3D 姿态不可用
- `vc_redist.x64.exe` 或目标机已安装匹配的 Microsoft Visual C++ Redistributable

以下内容不应作为正式发布包的必要内容：

- `obj/`, `moc/`, `rcc/`, `ui/` 等构建中间目录
- `*.pdb`，除非本次交付明确包含调试符号
- 本机临时日志，例如 `uia-enum-*.log`
- TensorRT 生成的 `.engine` 缓存，除非已确认目标 GPU、驱动、TensorRT/CUDA 版本与构建机器一致

交付前应在目标机或等效干净环境中复制 `x64/Release` 并启动 `iskating.exe`，确认 Qt 插件、FFmpeg、TensorRT/CUDA、模型目录和训练服务连接均可用。

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
- 确认未把构建中间目录、临时日志或未验证可跨机复用的 TensorRT engine 当作必要交付物。
- 确认目标机器 GPU/驱动支持 TensorRT/CUDA/D3D11VA。
- 确认 VC Redistributable 需求。

## 无法确认的信息

- TODO: 是否需要安装器。
- TODO: 是否需要预构建 TensorRT engine。
- TODO: 是否需要将 `rtmw3d-x.onnx` 放入发布包。
- TODO: 目标机器最低硬件和驱动版本。
