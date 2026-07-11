# Project Overview

上级入口：[[00-index|AI 知识库索引]]
相关文档：[[02-architecture|架构说明]]、[[03-commands|运行命令]]、[[07-open-questions|未确认问题]]
核心模块：[[modules/core|应用核心]]、[[modules/frontend|Qt Widgets 前端]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]
核心流程：[[flows/main-user-flow|主用户流程]]、[[flows/pose-analysis-flow|运动员检测与身份流程]]

## 项目是什么

`iskating` 是一个 Windows 桌面端 iSkating Coach 应用，面向冰上训练场景做多路摄像头预览、主视图采集、人体姿态识别、3D 骨架/轨迹展示、实时评分、训练历史和纠正建议。

相关文件：

- `main.cpp`: Qt 应用入口，设置运行库/插件搜索路径并启动 `MainWindow`。
- `mainwindow.cpp`: 主窗口、导航、训练采集、评分、历史和建议页的核心逻辑。
- `mainwindow.ui`: Qt Designer UI，包含主视频、12 路摄像头、历史页和建议页。

## 主要用户

主要用户看起来是冰上运动训练场景中的教练、运动员或训练系统操作员。

相关证据：

- `mainwindow.cpp`: 顶部显示 `iSkating Coach`、训练轮次、系统状态、模型状态、存储状态。
- `styles/iskating.qss`: 注释称视觉语言为暗色训练仪表盘。

TODO: 当前代码中无法确认实际用户角色、权限边界和使用场地要求。

## 核心业务目标

- 接入最多 12 路 RTSP 摄像头预览，并把选中机位主码流显示到主视图。
- 对活动视频流做 TensorRT YOLO26x person 检测和 PersonViT/MSMT17 ReID。
- 在当前 session 参与者 gallery 中匹配 athleteId，并维护 per-camera trackId。
- 保存检测框、身份状态和置信度；旧姿态、评分和动作记录保留读取兼容。

相关文件：

- `videoopenglwidget.cpp`
- `rtspstream.cpp`
- `handanalysismanager.cpp`
- `tensortrtathletebackend.cpp`
- `tensortrtathletebackend.cpp`
- `posestandardnessscorer.cpp`

## 主要技术栈

- C++20：`mainwindow.pro`
- Qt 6.7.3 Widgets/SVG：`mainwindow.pro`
- qmake/MSVC 2022 x64：`mainwindow.pro`
- FFmpeg/libav + D3D11VA：`rtspstream.cpp`, `d3d11videodevice.cpp`
- Direct3D 11/DXGI/D3DCompiler：`d3dvideosurface.cpp`
- TensorRT 10.1 + CUDA 11.8：`tensorrtrunner.cpp`, `mainwindow.pro`
- ONNX 模型：`models/athlete/`, `models/hand/`
- QSettings 本地配置与训练历史：`mainwindow.cpp`

## 主要入口文件

- `main.cpp`: 进程入口。
- `mainwindow.pro`: qmake 项目配置和构建/部署规则。
- `mainwindow.ui`: Qt Designer 页面结构。
- `mainwindow.cpp`: 主应用状态与用户操作入口。

## 主要目录

- `models/athlete/`: YOLO26x、PersonViT 模型清单、校验值和本地二进制文件。
- `models/hand/`: 手部模型配置与 ONNX 文件；当前主分析链路未直接接入。
- `icons/`: Qt 资源中引用的 SVG 图标。
- `images/`: Qt 资源中引用的装饰图片。
- `styles/`: QSS 样式。
- `tools/`: 模型下载、转换、校验和数据维护脚本。
- `x64/`: 本地构建输出，已在 `.gitignore` 中忽略，不应作为源码事实来源。

## 当前不确定信息

- TODO: 缺少 `README.md`，无法确认产品正式名称、发布流程和目标环境说明。
- TODO: 缺少测试目录，无法确认既有测试策略。
- TODO: 缺少 CI/CD 配置，无法确认自动构建或发布流程。
- TODO: `.vscode/` 中配置偏向 GCC/GDB，但 `mainwindow.pro` 强制 Qt MSVC 2022 x64；当前应以 `mainwindow.pro` 为准。
