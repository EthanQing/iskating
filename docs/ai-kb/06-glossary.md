# Glossary

上级入口：[[00-index|AI 知识库索引]]
相关文档：[[01-project-overview|项目概览]]、[[02-architecture|架构说明]]
相关模块：[[modules/core|应用核心]]、[[modules/video-streaming|视频流]]、[[modules/ai-inference|AI 推理]]、[[modules/persistence|本地持久化]]

## iSkating Coach

本项目的桌面训练分析应用名称。

相关文件：

- `main.cpp`
- `mainwindow.cpp`

## MainWindow

应用编排中心，负责 UI、摄像头配置、采集控制、训练记录和运动员检测/身份识别。

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

负责把 D3D11 视频纹理绘制到 Qt 控件上，并绘制运动员检测框覆盖层。

相关文件：

- `d3dvideosurface.h`
- `d3dvideosurface.cpp`

## TensorRtRunner

TensorRT 通用推理封装，负责从 ONNX 构建/加载 `.fp16.engine`、分配 IO buffer 和执行推理。

相关文件：

- `tensorrtrunner.h`
- `tensorrtrunner.cpp`

## AthleteAnalysisManager

运动员检测与身份识别后台管理器，负责多路流调度、YOLO26x/PersonViT 推理回调和结果 TTL。

相关文件：

- `athleteanalysismanager.h`
- `athleteanalysismanager.cpp`

## PersonViT ReID

基于 TransReID MSMT17 ViT-Base baseline 的人员重识别 embedding 模型，输出 768 维 L2 归一化向量。

相关文件：

- `tensortrtathletebackend.cpp`
- `models/athlete/athlete_models.json`
- `tools/convert_personvit_msmt17.py`

## AthleteFrameResult

一帧运动员检测结果，包含 cameraId、时间戳、画面尺寸、检测框、trackId、athleteId、身份状态和置信度。

相关文件：

- `athleteanalysisresult.h`

## PoseFrameResult

旧实时姿态结果结构，仅用于读取历史记录和兼容旧复盘。

相关文件：

- `poseresult.h`

## PoseStandardnessScorer

旧姿态评分器，不再由新实时采集流程调用；保留源码和历史字段以支持旧数据读取。

相关文件：

- `posestandardnessscorer.h`
- `posestandardnessscorer.cpp`

## QSettings

当前项目的本地配置机制，用于保存摄像头配置和采集偏好；训练历史与 ReID gallery 由 FastAPI/PostgreSQL 及服务端文件目录保存。

相关文件：

- `mainwindow.cpp`
- `main.cpp`
