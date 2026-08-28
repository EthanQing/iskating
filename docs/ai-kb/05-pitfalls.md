# 已知风险与坑点

[返回索引](00-index.md) · [排障 Runbook](runbooks/debugging.md) · [未确认问题](07-open-questions.md)

## P0：数据正确性

### Participant UUID 导致轨迹/速度漏存

客户端可能预生成 participant UUID；服务端 `save_session_participants()` 会把主运动员先插入并可能使用新的 UUID。`track_points`/`speed_metrics` 只接受服务端 participant 集合中的 ID，因此主运动员指标可能被静默跳过。

在修复并完成真实 PostgreSQL 集成测试前：

- 不能把实时轨迹绘制成功当作持久化闭环成功。
- 不能只验证 API 返回 200；必须查询 session 的 participant、track point 和 speed metric 外键。

### 不得伪造场地坐标

- `track_points` 只能来自有效四点单应性标定后的米制坐标。
- 旧 `participant_pose_frames.field_x/field_y`、bbox 像素或线性场地段配置不能冒充 F-24 轨迹。
- 未标定机位和 unknown 身份不产生场地点。

### 完整分析“完成”检查仍不完备

当前激活前检查涵盖分块哈希、块内范围/不重叠和登记帧数，但尚未完整强制：

- 全局 frameIndex 连续无 gap。
- PTS 跨块单调。
- JSONL 内容与登记元数据逐项一致。

因此数据库状态 `completed` 不等同于已达到最终产品级完整性门槛。

## P0：破坏性操作与安全

- `tools/reset_postgres_schema.py --yes` 会删除业务表；未确认数据库可丢弃时禁止执行。
- 摄像头模板和 QSettings 可包含明文 RTSP 密码；日志、截图、错误消息和提交内容必须脱敏。
- 默认管理员密码、默认 JWT secret 和空 worker token 只能用于开发，不能进入生产。
- `nas://` 映射必须阻止 `..` 或符号链接造成根目录逃逸。

## P1：构建和运行时强绑定

- Qt 路径固定为 `C:/Qt/6.7.3/msvc2022_64`，必须使用对应 qmake。
- FFmpeg、TensorRT 10.1、CUDA 11.8 有默认本机路径；可通过环境变量覆盖的仅见 Reference。
- D3D11VA 是当前硬要求，没有通用软件解码 fallback；RTSP 和本地导入都可能因编码/GPU 不兼容失败。
- `.fp16.engine` 与模型、TensorRT/CUDA、GPU 和驱动相关，不可假设跨机器复用。
- ONNX 二进制通常被忽略；构建成功不保证发布目录具备 AI 模型。
- 正在运行的 `iskating.exe` 可能锁住发布 DLL，导致 Release 复制失败。

## P1：语义容易混淆

- `trajectoryEnabled` 当前还影响 RTSP 机位是否进入 AI 分析，不只是轨迹显示开关。
- 检测 ROI 与四点场地标定独立；缺 ROI 默认 fail-open，不代表已限制在冰面。
- `trackId` 是单机位 ID，不是跨机位全局身份。
- `planned` 视频资产只是规范路径/引用，不表示真实录像已落盘。
- 单视频导入任务 100% 仅表示文件探测和登记完成，不表示全视频 AI 完成。
- 完整分析“暂停”只停止客户端轮询；远端 worker 仍可继续，取消才请求停止。
- 应用重启后任务可恢复为 `paused` 展示，但本地 job 参数未重建，不能直接继续。
- run 的模型/预处理/gallery 字段当前是标签，不是不可变资产快照。
- 完整分析激活不会自动创建训练 session。

## P1：线程和生命周期

- Windows 多路 AI 是一个 worker round-robin，不是 12 个并行模型实例。
- `RtspStream::stop()` 最后可能 terminate 线程；FFmpeg 阻塞、共享 D3D device 和析构顺序都需谨慎。
- TensorRT 初始化/销毁可能耗时很长；不要在 UI 线程执行。
- `AnalysisTaskManager` 在线程内使用独立 Repository；不要传入 UI 线程 Repository 或 QWidget。

## P2：历史兼容

- 服务端旧姿态/动作/评分接口仍存在，客户端当前不再绘制姿态关键点，也不应为新训练补虚假值。
- 新 person/ReID session 的零动作、零评分不能用于解释技术水平趋势。
- `joint_metrics` 有存储/查询/导出能力，但当前 AI 链路不生成新关节指标。
- 旧 SQLite 不会由客户端自动迁移；只能通过显式工具导入。
- 删除运动员/教练应归档，硬删可能破坏历史外键和复盘。

## P2：UI 和回放

- RTSP/NVR 回放通常不具备本地文件的精确 seek、慢放和逐帧能力。
- 离线原文件被移动、替换或删除后，历史引用仍在但回放可能失败。
- 历史缺口处必须清空上一帧检测框，不能沿用陈旧结果。
- 侧栏折叠只操作真实 `QFrame#sidebar`，不能通过 layout 的 parent 推断容器。
