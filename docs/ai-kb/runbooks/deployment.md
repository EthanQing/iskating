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
- 复制 `models/athlete` 的模型清单和校验文件到 `DESTDIR/models/athlete`；若源码模型目录存在 ONNX 二进制，也一并复制
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
- `models/athlete/athlete_models.json`
- `models/athlete/athlete_models.sha256`
- `models/athlete/yolo26x.onnx` 与 `models/athlete/personvit_msmt17_vit_base.onnx`，需按清单校验后放入发布目录
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

- 开发期确认 PostgreSQL 数据库已创建，并已手动执行 `python tools/reset_postgres_schema.py --yes` 重建空库 schema。
- 确认 FastAPI 服务可通过内网访问，例如 `http://训练服务器:8000/health`。
- 确认桌面端 `server/baseUrl` 指向训练服务，或设置 `ISKATING_API_BASE_URL`。
- 如需迁移旧数据，先执行 `tools/import_sqlite_to_postgres.py` 并核对导入数量。
- 确认 `x64/Release` 中存在 `iskating.exe`。
- 确认 `platforms/qwindows.dll` 等 Qt 插件已部署。
- 确认 FFmpeg DLL 存在，例如 `avcodec-62.dll`, `avformat-62.dll`。
- 确认 TensorRT/CUDA DLL 存在，例如 `nvinfer_10.dll`, `cudart64_110.dll`。
- 确认 `models/athlete` 两个 ONNX 文件存在且通过 `python tools/check_athlete_models.py`。
- 确认未把构建中间目录、临时日志或未验证可跨机复用的 TensorRT engine 当作必要交付物。
- 确认目标机器 GPU/驱动支持 TensorRT/CUDA/D3D11VA。
- 确认 VC Redistributable 需求。

## DeepStream 9 分析服务

1. 在 Ubuntu 24.04 安装 Docker、NVIDIA 驱动和 NVIDIA Container Toolkit，确认容器可访问 GPU。
2. 在 Windows 导出动态 batch 模型并把 `models/athlete/deepstream` 放到部署工作副本：

```powershell
powershell -ExecutionPolicy Bypass -File tools/export_deepstream_models.ps1
```

3. 在 FastAPI 主机设置 `ISKATING_ANALYSIS_NAS_ROOT` 和随机长令牌 `ISKATING_ANALYSIS_WORKER_TOKEN`，然后执行 `tools/backfill_full_rate_analysis.py` 或重建空库。
4. 在 Ubuntu 设置 API 地址、同一令牌、worker ID 和 NAS 主机路径：

```bash
export ISKATING_API_BASE_URL=http://api-host:8000
export ISKATING_ANALYSIS_WORKER_TOKEN='replace-me'
export ISKATING_ANALYSIS_WORKER_ID=deepstream-01
export ISKATING_ANALYSIS_NAS_ROOT=/srv/iskating
docker compose -f analysis_worker/compose.yml config
docker compose -f analysis_worker/compose.yml up -d --build
```

5. 确认 NAS 源目录对 worker 只读、结果目录可写；当前 compose 把统一根目录挂为可写，因为结果与源共用根。生产环境应通过 NAS ACL 限制 worker 只修改结果前缀。
6. Windows 客户端“完整分析”中配置同一 `nas://` 根对应的盘符/UNC 路径，导入 12 路 manifest 后验证创建、进度、重试、渐进回放和激活。

首次上线前必须完成 12 路 1080p60、至少 60 分钟压力测试和中断恢复测试。实时倍速不是首期门槛，但帧数一致、frameIndex 连续、PTS 不倒退、无未声明缺口、无 OOM 和分块 SHA256 正确是强制门槛。

## 无法确认的信息

- TODO: 是否需要安装器。
- TODO: 是否需要预构建 TensorRT engine。
- TODO: 是否需要预构建与目标 GPU 绑定的 TensorRT engine。
- TODO: 目标机器最低硬件和驱动版本。
