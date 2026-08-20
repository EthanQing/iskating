# 环境变量与本机配置

[Reference 地图](README.md) · [本地开发](../runbooks/local-development.md) · [部署](../runbooks/deployment.md)

## 环境变量

| 名称 | 使用方 | 默认/必需 | 用途 |
|---|---|---|---|
| `FFMPEG_ROOT` | qmake | 默认 `C:/Users/qc/zm/ffmpeg-8.0.1-full_build-shared` | FFmpeg shared dev 根目录 |
| `TENSORRT_ROOT` | qmake/运行时 | 默认 `C:/Program Files/TensorRT-10.1.0.27` | TensorRT SDK |
| `CUDA_ROOT` | qmake/运行时 | 默认 `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8` | CUDA Toolkit |
| `ISKATING_DATABASE_URL` | FastAPI/数据工具 | **必需** | PostgreSQL SQLAlchemy URL |
| `ISKATING_JWT_SECRET` | FastAPI | 默认不安全值 | JWT HS256 签名 |
| `ISKATING_ADMIN_USER` | FastAPI seed | `admin` | 首次默认管理员名 |
| `ISKATING_ADMIN_PASSWORD` | FastAPI seed | `admin123` | 首次默认管理员密码 |
| `ISKATING_CORS_ORIGINS` | FastAPI | `*` | 逗号分隔 CORS origins |
| `ISKATING_IDENTITY_GALLERY_ROOT` | FastAPI | `data/identity-gallery` | ReID 样本文件根 |
| `ISKATING_ANALYSIS_NAS_ROOT` | FastAPI | `data/analysis-nas` | API 解析 `nas://` 的根 |
| `ISKATING_ANALYSIS_NAS_ROOT` | compose host | **必需** | 宿主 NAS 根，挂到容器 `/mnt/iskating` |
| `ISKATING_ANALYSIS_WORKER_TOKEN` | FastAPI/worker | 完整分析必需 | worker 专用令牌，两端相同 |
| `ISKATING_ANALYSIS_WORKER_ID` | worker | 主机名 / compose `deepstream-01` | 租约与诊断标识 |
| `ISKATING_ANALYSIS_POLL_SECONDS` | worker | `5` | 无任务时轮询秒数，最小 1 |
| `ISKATING_DEEPSTREAM_BINARY` | worker | `/opt/iskating/bin/iskating-fullrate` | 原生管线路径 |
| `ISKATING_API_BASE_URL` | 客户端/worker | 客户端 `http://127.0.0.1:8000`；worker 必需 | FastAPI 地址 |
| `ISKATING_API_TOKEN` | 客户端 | 空 | bearer token 覆盖 |
| `ISKATING_API_USERNAME` | 客户端 | `admin` | 自动登录账号 |
| `ISKATING_API_PASSWORD` | 客户端 | `admin123` | 自动登录密码 |
| `QT_PLUGIN_PATH` | 客户端 | 进程内设置 | Qt 插件搜索路径 |
| `PATH` | 客户端 | 保留系统值并前置本地目录 | DLL 搜索路径 |

安全要求：生产必须替换默认管理员密码/JWT secret，worker token 使用长随机值；不要在日志、命令历史或文档提交中保留真实凭据。

## 固定/重要路径

- Qt 根固定在 `mainwindow.pro`：`C:/Qt/6.7.3/msvc2022_64`，当前无环境变量覆盖。
- 客户端模型：`models/athlete`；ONNX 通常不提交。
- DeepStream 模型：`models/athlete/deepstream`；动态 batch 导出产物通常不提交。
- 构建输出：`x64/Debug`、`x64/Release`。
- `tensorrtrunner.cpp` 还会添加默认 TensorRT/CUDA DLL 目录，调整 SDK 路径时需一起检查。

## QSettings

应用名/组织名：`iSkating Coach` / `iSkating`。物理位置由 Qt/Windows 决定。

### 服务与认证

- `server/baseUrl`
- `auth/accessToken`
- `auth/username`
- `auth/password`

读取优先级由 `TrainingRepository` 实现：部分已保存 QSettings 值优先于环境变量。排障时检查旧 QSettings 是否遮蔽环境设置。

### 摄像头公共配置

- `cameraDefaults/username|password|port`
- `cameraDefaults/previewPath|previewFps`
- `cameraDefaults/mainPath|mainFps`
- `cameraDefaults/nvrPlaybackTemplate`

### 分析与存储

- `capture/modelPrecision|fps`
- `capture/analysisSource|analysisTargetFps|analysisMaxStreams|analysisAutoDegrade`
- `videoStorage/rootDir|capacityLimitGb|retentionDays`
- `offlineVideo/lastDir`
- `offlineAnalysis/nasRoot`

### 每路相机

`cameras/camera01` 到 `cameras/camera12` 分组，主要字段：

- `name`、`ip`、`port`、`path`
- `previewUrl`、`mainUrl`、兼容 `url`
- `trajectoryEnabled`、`role`
- `fieldStartM`、`fieldEndM`、`lateralOffsetM`
- `mountHeightM`、`yawDeg`、`pitchDeg`
- `calibration`、`qualityNote`、`compatibilityNote`

配置模板可含密码，按敏感文件处理。`trajectoryEnabled` 目前同时影响该 RTSP 机位是否进入 AI。
