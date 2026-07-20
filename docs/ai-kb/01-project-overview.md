# Project Overview

上级入口：[[00-index|AI 知识库索引]]
相关文档：[[02-architecture|架构说明]]、[[03-commands|运行命令]]、[[07-open-questions|未确认问题]]
核心模块：[[modules/core|应用核心]]、[[modules/frontend|Qt Widgets 前端]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]
核心流程：[[flows/main-user-flow|主用户流程]]、[[flows/pose-analysis-flow|运动员检测与身份流程]]

## 项目是什么

`iskating` 是面向冰上训练的多端系统：Windows Qt 客户端负责训练准备、多路视频、本机 AI、复盘与报告；FastAPI/PostgreSQL 负责训练业务和分析协议；Ubuntu DeepStream worker 负责 12 路 NAS 视频的完整帧率离线分析。

当前 Windows 实时主链路是 YOLO26x person 检测、PersonViT ReID、机位内 track，以及条件式二维轨迹/速度。新训练不生成姿态关键点、3D 骨架、自动动作或技术评分；旧姿态、动作和评分仅保留历史兼容。

相关文件：

- `main.cpp`: Qt 应用入口，设置运行库/插件搜索路径并启动 `MainWindow`。
- `mainwindow.cpp`: 主窗口、导航、训练准备、实时/单视频、历史、报告和建议的核心编排。
- `mainwindow.ui`: Qt Designer UI，包含主视频、12 路摄像头、历史页和建议页。
- `server/app/main.py`: FastAPI 路由、认证、PostgreSQL 读写与完整分析协议。
- `analysis_worker/`: Ubuntu/DeepStream 完整帧率 worker。

## 主要用户

主要用户看起来是冰上运动训练场景中的教练、运动员或训练系统操作员。

相关证据：

- `mainwindow.cpp`: 顶部显示 `iSkating Coach`、训练轮次、系统状态、模型状态、存储状态。
- `styles/iskating.qss`: 注释称视觉语言为暗色训练仪表盘。

TODO: 当前代码中无法确认实际用户角色、权限边界和使用场地要求。

## 核心业务目标

- 管理运动员、ReID 样本、教练关系、动作标准、比赛/场次和本次训练上下文。
- 接入最多 12 路 RTSP 预览和主码流，在 Windows 上做低延迟 person/ReID/机位内 track。
- 对已识别运动员和已四点标定机位计算场地米制二维轨迹与速度。
- 提供本地单视频探测/跟随播放分析，以及 12 路 NAS 视频的远端完整帧率 person/ReID 分析。
- 保存 session、视频引用、参与者、检测/身份摘要和条件式轨迹/速度，并提供历史检索、兼容复核、报告和趋势。

相关文件：

- `videoopenglwidget.cpp`
- `rtspstream.cpp`
- `athleteanalysismanager.cpp`
- `tensortrtathletebackend.cpp`
- `trajectorywidget.cpp`
- `trainingrepository.cpp`
- `server/app/main.py`
- `analysis_worker/worker.py`

## 主要技术栈

- C++20：`mainwindow.pro`
- Qt 6.7.3 Widgets/SVG：`mainwindow.pro`
- qmake/MSVC 2022 x64：`mainwindow.pro`
- FFmpeg/libav + D3D11VA：`rtspstream.cpp`, `d3d11videodevice.cpp`
- Direct3D 11/DXGI/D3DCompiler：`d3dvideosurface.cpp`
- TensorRT 10.1 + CUDA 11.8：`tensorrtrunner.cpp`, `mainwindow.pro`
- ONNX 模型：`models/athlete/`；`models/hand/` 是未接入的旧资产
- QtNetwork + FastAPI + PostgreSQL：`trainingrepository.cpp`, `server/requirements.txt`
- Ubuntu 24.04 + DeepStream 9 + Docker Compose：`analysis_worker/`
- NAS gzip JSONL 分块 + PostgreSQL 索引：`server/app/analysis_artifacts.py`, `server/app/schema.py`
- QSettings 只保存本机摄像头、分析、存储和服务连接等配置；训练业务数据位于 PostgreSQL

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
- `server/`: FastAPI、PostgreSQL schema 和 Python 单元测试。
- `analysis_worker/`: DeepStream worker、原生管线、容器与配置。
- `docs/`: 功能盘点、用户指南、原型和 AI 知识库。
- `x64/`: 本地构建输出，已在 `.gitignore` 中忽略，不应作为源码事实来源。

## 当前不确定信息

- TODO: 没有 CI/CD、安装器或自动发布流程。
- TODO: 已有 `server/tests` 的协议/纯逻辑测试，但缺少真实 PostgreSQL/API、Qt UI、GPU/DeepStream 和 12 路压测 E2E。
- TODO: 正式用户角色、角色授权、产品验收标准和生产发布流程待确认。
- TODO: `.vscode/` 中配置偏向 GCC/GDB，但 `mainwindow.pro` 强制 Qt MSVC 2022 x64；当前应以 `mainwindow.pro` 为准。
