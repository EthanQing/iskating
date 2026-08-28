# 12 路完整帧率分析流程

[流程地图](README.md) · [完整帧率 Worker](../modules/fullrate-worker.md) · [数据模型](../references/database-schema.md)

## 输入合同

- 一个 batch 恰好包含 12 路 `nas://` 源。
- 每路包含 cameraId、源开始时间、媒体信息和可选人工毫秒校正。
- Windows、FastAPI 和 worker 对同一 `nas://` URI分别配置物理根映射。
- run 保存模型、预处理、同步和 gallery hash 标签；当前不是冻结快照。

## 创建与执行

1. 客户端导入/校验 manifest，创建通用 `full_rate_batch` 任务和 batch/source 记录。
2. 客户端创建 run；状态进入 queued。
3. worker 使用独立 token claim run 并获得租约、12 路源和 gallery。
4. worker 将 gallery 写临时 TSV，解析所有 NAS 路径，启动原生 DeepStream 管线。
5. 每帧执行 YOLO，NvDCF 维护单机位 track，PersonViT 按策略执行。
6. 统一时间：`batchTime = source PTS + source start offset + manual correction`。
7. 原生管线约每 10 秒写一个 gzip JSONL；完成原子重命名后输出 CHUNK 事件。
8. Python worker 计算 SHA256，通过 API 幂等登记 chunk，并持续 heartbeat。
9. 每路结束后登记总帧数；单路失败保留其他源已提交 chunk，run 可为 partial/failed。

## 查询、重试、取消和激活

- 范围查询只读取已登记且校验通过的 chunk，并返回 coverage gaps。
- 客户端渐进回放时，未完成/缺口区间必须清空旧 bbox。
- 取消设置服务端请求；worker heartbeat 后终止原生进程。
- 当前 retry 就地重置同一 run，不创建新的 run 版本入口。
- 只有 12 路完成并通过现有完整性检查后可显式激活。
- 激活更新 batch 当前 run 和已关联 session；不会创建新 session。

## 当前完整性检查

已实现：

- chunk 文件存在与 SHA256。
- chunk 内范围和 source 内不重叠。
- 已登记 frameCount 与源统计的基本一致性。

尚未形成最终强制合同：

- 全局 frameIndex 从首帧到末帧连续。
- PTS 跨 chunk 单调。
- JSONL 每条内容与 chunk 元数据逐项一致。

## 输出边界

完整结果包含每解码帧 bbox、cameraId、单机位 track、身份和置信度。不包含轨迹、速度、姿态、动作或评分。关联 session 时服务端最多派生约 200ms 的 bbox/track/身份兼容摘要。

## 暂停语义

任务中心“暂停”目前只停止 Windows 轮询并更新通用任务状态，不向远端 worker 发送暂停；worker 可继续处理。界面和文档不得承诺远端已暂停。

## 验收重点

- 用带已知 frameIndex 的 12 路输入核对解码帧数和结果帧数。
- 验证 PTS、batchTime、人工校正和缺口。
- 中断 worker、FastAPI、NAS 后验证租约/重试、幂等 chunk 和无静默缺帧。
- 记录 GPU/CPU/显存/NAS I/O/吞吐和 ReID 调用率。
