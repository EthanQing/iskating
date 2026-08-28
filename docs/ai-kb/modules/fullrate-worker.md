# 完整帧率 Worker 模块

[模块地图](README.md) · [完整分析流程](../flows/fullrate-analysis.md) · [部署 Runbook](../runbooks/deployment.md)

## 职责

Ubuntu/DeepStream worker 对一个 batch 的 12 路 NAS 视频逐解码帧执行 person/track/ReID，并把结果分块写回 NAS。它与 Windows latest-frame 实时 AI 完全独立。

## 组成

- `analysis_worker/worker.py`：领取 run、gallery 文件、原生子进程、heartbeat、chunk 校验/登记和失败处理。
- `analysis_worker/api.py`：worker API 客户端。
- `analysis_worker/model_contract.py`：模型契约读取/校验。
- `analysis_worker/native/fullrate_pipeline.cpp`：DeepStream 原生管线与 JSONL 产物。
- `analysis_worker/custom_parser/yolo26x_parser.cpp`：YOLO26x NMS-free 输出 parser。
- `analysis_worker/config/*.txt`：PGIE/SGIE 配置。
- `analysis_worker/Dockerfile`、`compose.yml`：DeepStream 9 容器构建和部署。

## 执行协议

1. 以 worker token 从 FastAPI claim 一个 run。
2. 将 run gallery 写入临时 TSV。
3. 将每路 `nas://` URI约束解析到 `ISKATING_ANALYSIS_NAS_ROOT`。
4. 计算 `sourceOffsetMs` 和 `manualCorrectionMs`，调用原生管线。
5. 每 30 秒 heartbeat；服务端返回取消请求时 terminate 子进程。
6. 收到 `CHUNK` 事件后读取已完成文件、计算 SHA256 并登记。
7. 收到 `FINISH` 后提交每路总帧数；异常源提交错误。

## 管线约束

- YOLO 对每个解码帧运行，`interval=0`。
- 队列不得 leaky/drop；性能不足必须背压。
- NvDCF 的 track 是单机位轨迹。
- PersonViT 按新 track、周期或低置信度条件执行，身份沿 track 传播。
- PTS 缺失或倒退应使源失败，不静默补帧。
- 逐帧输出包含 frameIndex、PTS、batchTime、cameraId、画面尺寸和对象列表。
- 每约 10 秒一个 gzip JSONL chunk；只有原子完成且 SHA256 已登记的文件属于有效结果。

## 不负责

- 不生成场地轨迹、速度、姿态关键点、动作计数或技术评分。
- 不创建训练 session。
- 不冻结模型和 gallery 快照；当前 run 字段只是标签。
- 不处理 Windows QSettings 或 RTSP 实时预览。

## 存储与安全

- `nas://` 是逻辑 URI，必须限制在配置根目录。
- 容器默认把 NAS 根挂到 `/mnt/iskating`，模型只读挂到 `/opt/iskating/models`。
- 当前源与结果共用可写根；生产环境需通过 NAS ACL 限制 worker 只修改结果前缀。
- Engine 放独立 Docker volume，不提交 Git。

## 修改与验证

- Python 编排：运行 `server/tests/test_analysis_worker.py`、模型合同和 artifact 测试。
- Parser/管线：在 DeepStream 9 NVIDIA 环境构建并用固定输入验证坐标、frameIndex、PTS 和对象数。
- Docker：运行 `docker compose -f analysis_worker/compose.yml config`；通过不代表镜像可拉取或 GPU 管线可运行。
- 上线：需要 12 路已知帧号 E2E、1080p60 压测、worker/API/NAS 中断恢复和分块连续性检查。
