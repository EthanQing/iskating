# 第三方依赖与模型

[Reference 地图](README.md) · [本地开发](../runbooks/local-development.md) · [视频与实时 AI](../modules/video-and-realtime-ai.md)

## Windows 客户端依赖

| 依赖 | 当前合同 | 用途 |
|---|---|---|
| Qt | 6.7.3 MSVC 2022 x64 | Core/Gui/Widgets/SVG/Network/PrintSupport |
| MSVC | Visual Studio 2022 x64，C++20 | 客户端构建 |
| FFmpeg | shared MSVC x64；当前部署 DLL major 见 `deployment.pri` | RTSP/本地文件、D3D11VA |
| Direct3D | D3D11/DXGI/D3DCompiler | 硬件 frame 与渲染 |
| TensorRT | 10.1.0.27 | ONNX 解析、FP16 engine、推理 |
| CUDA | 11.8 | TensorRT runtime/buffer |
| QXlsx | 仓库 vendored，MIT | XLSX 导出 |

准确路径和链接库见 `build/qmake/dependencies.pri`，部署复制规则见 `build/qmake/deployment.pri`。

## 服务端依赖

`server/requirements.txt` 固定：

- FastAPI 0.111.1
- SQLAlchemy 2.0.31
- psycopg 3.2.1 binary
- Uvicorn 0.30.3
- PyJWT 2.8.0
- passlib 1.7.4 + bcrypt 4.0.1

`bcrypt==4.0.1` 是为避免 passlib 与较新 bcrypt 的兼容问题，不要无验证升级。

## DeepStream

- 容器基于 `nvcr.io/nvidia/deepstream:9.0-triton-multiarch`。
- 目标环境：Ubuntu 24.04、兼容 NVIDIA 驱动、Docker、NVIDIA Container Toolkit。
- 原生构建使用 CMake/g++/GStreamer 开发包/zlib。
- 生产吞吐依赖具体 GPU 和 NAS，仓库没有最低硬件基线。

## 模型合同

权威 manifest：`models/athlete/athlete_models.json`。

### YOLO26x

- 来源：Ultralytics release `yolo26x.pt`。
- ONNX：`yolo26x.onnx`。
- 输入：`1x3x640x640`。
- 输出：`1x300x6`，端到端 NMS-free。
- 类别：COCO person class 0。
- 默认阈值：0.35。
- 许可：Ultralytics AGPL-3.0 或 Enterprise，发布前需确认适用方案。

### PersonViT / TransReID

- 来源：TransReID MSMT17 ViT-Base baseline。
- ONNX：`personvit_msmt17_vit_base.onnx`。
- 输入：`1x3x256x128`，RGB，mean/std 均为 0.5。
- 输出：768 维，并做 L2 normalize。
- 模型版本：`personvit-msmt17-vit-base-v1`。
- 预处理版本：`rgb-256x128-mean0.5-std0.5-l2-v1`。
- 许可：遵守 TransReID 仓库许可和 MSMT17 数据集条款。

### 运行时阈值

- ReID threshold：0.60。
- ambiguous margin：0.05。
- result TTL：350ms。
- track TTL：1200ms。
- track IoU：0.20。
- ReID 最少 track hits：2；最多尝试 3；重试 1000ms。
- ROI 默认启用，配置 `camera_detect_rois.json`。

## 模型交付

- ONNX 二进制和 `.fp16.engine` 不应作为普通 Git 源码提交。
- 下载/转换：`tools/download_athlete_models.ps1`、`convert_personvit_msmt17.py`。
- 校验：`tools/check_athlete_models.py` + `athlete_models.sha256`。
- DeepStream 动态 batch 导出：`tools/export_deepstream_models.ps1`。
- Engine 与 GPU、驱动、TensorRT/CUDA 和模型绑定，默认在目标机重建。

## 外部服务现状

- PostgreSQL：训练业务数据库。
- RTSP 摄像头/NVR：视频源与可能的历史回放。
- NAS：原始完整分析视频与结果分块。
- 未发现支付、邮件、云对象存储、日志聚合或监控服务。
