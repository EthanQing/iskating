# 项目概览

[返回索引](00-index.md) · [系统架构](02-architecture.md) · [已知风险](05-pitfalls.md)

## 项目定位

`iskating` 是冰上训练辅助系统，包含：

- Windows Qt 桌面客户端：训练准备、多路视频、本机实时 AI、训练保存、历史复盘和报告。
- FastAPI/PostgreSQL 训练服务：认证、人员/比赛/标准、训练 session、指标和完整分析协议。
- Ubuntu DeepStream worker：对 NAS 中恰好 12 路同步录像做完整帧率离线分析。

主要使用者从代码只能确认是教练、运动员和现场操作人员；正式角色权限和产品验收矩阵仍待确认。

## 当前能力边界

### 已实现

- 最多 12 路 RTSP 预览和主码流显示，FFmpeg + D3D11VA 解码，UDP/TCP fallback 与断流重连。
- Windows 实时 AI：YOLO26x person 检测、按机位 ROI 过滤、单机位 track、按需 PersonViT ReID。
- 当前 session 最多 4 名参与者 gallery 与人工 track 绑定。
- 对“已识别运动员 + 有效四点场地标定”生成二维米制轨迹和速度序列。
- 本地单视频探测、硬解播放、seek/慢放/逐帧，以及跟随播放的低帧率实时 AI。
- 训练人员、ReID 样本、比赛/场次、动作标准、session、视频引用、历史筛选、复盘和报告。
- 12 路 NAS 视频完整帧率 person/track/ReID 分析；逐帧结果写 gzip JSONL 分块，数据库保存索引与状态。

### 当前不提供

新训练不生成：

- 姿态关键点或 3D 骨架。
- 自动动作计数。
- 新的技术评分或关节指标。
- 自动跨机位全局 track。
- RTSP 实际录像、分段或文件复制。

服务端保留旧姿态、动作、评分和关节指标表/接口用于历史兼容；这不代表当前实时链路仍生成这些数据。

## 三种分析入口

| 入口 | 执行位置 | 输入 | 输出 | 关键限制 |
|---|---|---|---|---|
| RTSP 实时训练 | Windows 客户端 | 最多 12 路活动流 | bbox、身份、track；条件式轨迹/速度 | 单个 AI worker 轮询，默认目标 5 FPS/路 |
| 本地单视频 | Windows 客户端 | 一个本地文件 | 同实时链路 | 导入完成只表示探测/登记完成；开始采集后才做 AI |
| 完整帧率分析 | Ubuntu worker | 恰好 12 路 `nas://` 视频 | 每解码帧 bbox、track、ReID 分块 | 不生成轨迹、速度、姿态、动作或评分；完成不自动创建 session |

## 技术栈

- C++20、Qt 6.7.3 Widgets/Network/PrintSupport/SVG、qmake、MSVC 2022 x64。
- FFmpeg shared、D3D11/DXGI/D3DCompiler、D3D11VA。
- TensorRT 10.1、CUDA 11.8、YOLO26x、TransReID PersonViT。
- Python、FastAPI、SQLAlchemy、psycopg、PostgreSQL、JWT。
- Ubuntu 24.04、NVIDIA DeepStream 9、Docker Compose、NAS。
- vendored QXlsx。

精确版本与模型契约见 [第三方依赖与模型](references/third-party-services.md)。

## 代码入口

```text
mainwindow.pro                  qmake 入口
build/qmake/                    公共编译、依赖、部署规则
src/app/                        进程入口
src/ui/                         Qt Widgets、Designer UI、对话框
src/domain/                     领域数据结构
src/application/                实时 AI 与分析任务编排
src/infrastructure/video/       视频、D3D、NVR、离线探测
src/infrastructure/inference/   TensorRT、ROI、track、ReID
src/infrastructure/persistence/ FastAPI 客户端、视频资产规划
src/infrastructure/configuration/ 摄像头模板与连通测试
server/                         FastAPI、schema、Python 测试
analysis_worker/                DeepStream worker 与原生管线
tests/client/                   客户端纯逻辑合同测试
tools/                          模型与数据维护工具
models/athlete/                 模型 manifest/校验/ROI；二进制通常忽略
resources/                      QSS、图标、图片、qrc
```

## 主要事实源

- 构建与依赖：`mainwindow.pro`、`build/qmake/*.pri`
- 客户端业务编排：`src/ui/mainwindow.cpp`
- 领域结构：`src/domain/trainingdomain.h`、`src/domain/athleteanalysisresult.h`
- 服务端契约：`server/app/main.py`、`server/app/schema.py`
- Worker：`analysis_worker/worker.py`、`analysis_worker/native/`
- 模型契约：`models/athlete/athlete_models.json`
