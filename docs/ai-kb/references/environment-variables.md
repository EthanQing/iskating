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
| `QT_PLUGIN_PATH` | 否 | 运行时 Qt 插件搜索路径；程序会在启动时设置 | 程序目录和 `plugins` 子目录 | `main.cpp` |
| `PATH` | 否 | 运行时 DLL 搜索路径；程序会把本地目录前置 | 保留系统原值 | `main.cpp` |

## 不是环境变量但很重要的路径

- `QT_ROOT = C:/Qt/6.7.3/msvc2022_64` 写死在 `mainwindow.pro`。
- `tensorrtrunner.cpp` 还会通过 `AddDllDirectory()` 添加默认 TensorRT/CUDA DLL 路径。

## 未确认信息

- TODO: 未确认是否允许把 `QT_ROOT` 改成环境变量。
- TODO: 未确认目标部署机是否依赖系统级 PATH。
