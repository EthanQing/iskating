# Glossary

上级入口：[[00-index|AI 知识库索引]]
相关文档：[[01-project-overview|项目概览]]、[[02-architecture|架构说明]]
相关模块：[[modules/core|应用核心]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/pose-analysis|姿态分析]]、[[modules/persistence|本地持久化]]

## iSkating Coach

本项目的桌面训练分析应用名称。

相关文件：

- `main.cpp`
- `mainwindow.cpp`

## MainWindow

应用编排中心，负责 UI、摄像头配置、采集控制、训练记录、评分和建议。

相关文件：

- `mainwindow.h`
- `mainwindow.cpp`

## CAM 01-12

界面中的 12 路摄像头通道，每路通常只配置 IP，公共 RTSP 参数由系统设置统一维护。

相关文件：

- `mainwindow.ui`
- `mainwindow.cpp`
- `systemsettingsdialog.cpp`

## Preview Stream / 预览子码流

12 路小窗使用的低负载视频流。

相关文件：

- `systemsettingsdialog.h`
- `mainwindow.cpp`
- `videoopenglwidget.cpp`

## Main Stream / 主码流

主视图用于显示和 AI 分析的视频流。主码流失败时，主视图可在特定 D3D11 错误场景回退到预览流。

相关文件：

- `videoopenglwidget.cpp`
- `mainwindow.cpp`

## RtspStream

后台线程中的视频源读取器，使用 FFmpeg 打开 RTSP 或本地文件，并输出 D3D11 硬件帧。

相关文件：

- `rtspstream.h`
- `rtspstream.cpp`

## D3DFrame

封装 FFmpeg D3D11 硬件帧、纹理、尺寸和时间戳的数据结构。

相关文件：

- `d3dframe.h`

## D3DVideoSurface

负责把 D3D11 视频纹理绘制到 Qt 控件上，并绘制姿态覆盖层。

相关文件：

- `d3dvideosurface.h`
- `d3dvideosurface.cpp`

## TensorRtRunner

TensorRT 通用推理封装，负责从 ONNX 构建/加载 `.fp16.engine`、分配 IO buffer 和执行推理。

相关文件：

- `tensorrtrunner.h`
- `tensorrtrunner.cpp`

## Body17

项目当前人体姿态主干使用的 17 点骨架格式，来自 YOLOv8 pose 和 RTMW3D 的身体关键点子集。

相关文件：

- `poseresult.h`
- `poseresult.cpp`
- `tensorrtbodyposebackend.cpp`

## RTMW3D

用于补充 3D 姿态关键点的 TensorRT 后端。模型文件为 `rtmw3d-x.onnx`。

相关文件：

- `tensorrtrtmw3dbackend.cpp`
- `models/body/body_model.json`
- `tools/download_rtmw3d_x.ps1`

## PoseFrameResult

一帧姿态结果的统一数据结构，包含相机编号、时间戳、画面尺寸、实例和关键点。

相关文件：

- `poseresult.h`

## PoseStandardnessScorer

根据姿态结果计算关键点、对称、重心、稳定和 3D 分项分数，输出总分和反馈。

相关文件：

- `posestandardnessscorer.h`
- `posestandardnessscorer.cpp`

## QSettings

当前项目的本地持久化机制，用于保存摄像头配置、采集偏好和训练历史。

相关文件：

- `mainwindow.cpp`
- `main.cpp`
