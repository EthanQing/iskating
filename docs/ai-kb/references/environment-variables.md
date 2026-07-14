# Environment Variables

上级入口：[[00-index|AI 知识库索引]]、[[references/README|Reference 地图]]
相关命令：[[03-commands|运行命令]]
相关 Runbook：[[runbooks/local-development|本地开发]]、[[runbooks/deployment|部署]]
相关风险：[[05-pitfalls|坑点]]

项目没有 `.env.example`。以下变量来自 `mainwindow.pro` 和运行时代码。

| 名称 | 是否必需 | 用途 | 默认值 | 相关文件 |
|---|---|---|---|---|
| `FFMPEG_ROOT` | 否 | 覆盖 FFmpeg shared MSVC x64 dev package 根目录 | `C:/Users/qc/zm/ffmpeg-8.0.1-full_build-shared` | `mainwindow.pro` |
| `TENSORRT_ROOT` | 否 | 覆盖 TensorRT SDK 根目录 | `C:/Program Files/TensorRT-10.1.0.27` | `mainwindow.pro` |
| `CUDA_ROOT` | 否 | 覆盖 CUDA Toolkit 根目录 | `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8` | `mainwindow.pro` |
| `ISKATING_DATABASE_URL` | 服务端必需 | FastAPI 和 schema reset 脚本连接 PostgreSQL | 无 | `server/app/main.py`, `tools/reset_postgres_schema.py` |
| `ISKATING_JWT_SECRET` | 服务端必需 | JWT 签名密钥 | `change-me-before-production` | `server/app/main.py` |
| `ISKATING_ADMIN_USER` | 否 | 首次 seed 默认管理员用户名 | `admin` | `server/app/main.py` |
| `ISKATING_ADMIN_PASSWORD` | 否 | 首次 seed 默认管理员密码 | `admin123` | `server/app/main.py` |
| `ISKATING_CORS_ORIGINS` | 否 | FastAPI CORS 来源 | `*` | `server/app/main.py` |
| `ISKATING_IDENTITY_GALLERY_ROOT` | 否 | ReID 样本图片文件存储目录 | `data/identity-gallery` | `server/app/main.py` |
| `ISKATING_ANALYSIS_NAS_ROOT` | 离线分析必需 | FastAPI 或 worker 上 `nas://` 对应的本机 NAS 根目录；容器内固定为 `/mnt/iskating` | 服务端 `data/analysis-nas`；worker 无 | `server/app/main.py`, `analysis_worker/worker.py`, `analysis_worker/compose.yml` |
| `ISKATING_ANALYSIS_WORKER_TOKEN` | 离线分析必需 | worker 专用 API 令牌；FastAPI 与 worker 值必须一致 | 无 | `server/app/main.py`, `analysis_worker/worker.py` |
| `ISKATING_ANALYSIS_WORKER_ID` | 否 | worker 租约和诊断标识 | 主机名或 `deepstream-01` | `analysis_worker/worker.py`, `analysis_worker/compose.yml` |
| `ISKATING_ANALYSIS_POLL_SECONDS` | 否 | 无任务时领取轮询间隔 | `5` | `analysis_worker/worker.py` |
| `ISKATING_API_BASE_URL` | 否 | 桌面端训练服务地址覆盖 | `http://127.0.0.1:8000` | `trainingrepository.cpp` |
| `ISKATING_API_TOKEN` | 否 | 桌面端访问令牌覆盖 | 无 | `trainingrepository.cpp` |
| `ISKATING_API_USERNAME` | 否 | 桌面端自动登录用户名 | `admin` | `trainingrepository.cpp` |
| `ISKATING_API_PASSWORD` | 否 | 桌面端自动登录密码 | `admin123` | `trainingrepository.cpp` |
| `QT_PLUGIN_PATH` | 否 | 运行时 Qt 插件搜索路径；程序会在启动时设置 | 程序目录和 `plugins` 子目录 | `main.cpp` |
| `PATH` | 否 | 运行时 DLL 搜索路径；程序会把本地目录前置 | 保留系统原值 | `main.cpp` |

## 不是环境变量但很重要的路径

- `QT_ROOT = C:/Qt/6.7.3/msvc2022_64` 写死在 `mainwindow.pro`。
- `tensorrtrunner.cpp` 还会通过 `AddDllDirectory()` 添加默认 TensorRT/CUDA DLL 路径。
- `models/athlete` 是桌面端模型目录；二进制模型文件被忽略，部署时需要按 `tools/download_athlete_models.ps1` 准备。
- `models/athlete/deepstream` 保存动态 batch ONNX 和校验元数据，同样被忽略；使用 `tools/export_deepstream_models.ps1` 生成。
- Windows `QSettings offlineAnalysis/nasRoot` 不是环境变量，用于把可移植 `nas://` URI 映射到盘符或 UNC 根目录。

## 未确认信息

- TODO: 未确认是否允许把 `QT_ROOT` 改成环境变量。
- TODO: 未确认目标部署机是否依赖系统级 PATH。
