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

Qt 根目录当前写在 `mainwindow.pro`，不是环境变量。

## 数据库准备

无需数据库。配置和训练历史通过 `QSettings` 自动创建。

相关文件：

- `mainwindow.cpp`
- `main.cpp`

## 模型准备

- `models/body/yolov8n-pose.onnx` 已在仓库中。
- `models/body/rtmw3d-x.onnx` 很大，可能需要运行 `tools/download_rtmw3d_x.ps1`。
- 首次运行 TensorRT 可能生成 `.fp16.engine`。

## 常见启动失败原因

- qmake 不是 `C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe`。
- FFmpeg/TensorRT/CUDA 默认路径不存在，且没有设置覆盖环境变量。
- 输出目录缺少 Qt platforms 插件或 FFmpeg/TensorRT/CUDA DLL。
- GPU/驱动不支持当前 TensorRT/CUDA 或 D3D11VA 路径。
- `models/body/rtmw3d-x.onnx` 缺失导致 3D 姿态不可用。
