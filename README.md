# iSkating Coach

## F-24 二维滑行轨迹

完成每路相机的四点冰面标定后，应用将检测框底边投影到统一场地米制坐标，实时绘制并在保存训练时写入 `track_points`。坐标原点位于场地起点冰面，`+x` 指向滑行方向，`+y` 为预先约定的横向正方向，`z=0`。在系统设置的场地表双击“四点标定”单元格，输入四个像素点及其对应的场地 `(x,y)`；未标定的机位不会产生轨迹点。历史卡片的“姿态轨迹”入口可重建二维路线并导出 CSV/XLSX。

## F-25 滑行速度序列

每个有效轨迹点都关联一条 `speed_metrics` 记录，包含瞬时速度、1000ms 平滑速度、单位（`m/s`）、算法版本和有效标记。首点、时间异常、超过 2 秒的取样间隔或无效检测会保留为无效速度结果。历史轨迹复盘可按运动员筛选并查看、导出速度曲线；旧训练记录不会补写速度。

Windows Qt/C++ 滑冰训练辅助应用，当前实时 AI 主流程为：`YOLO26x person 检测 → PersonViT/MSMT17 ReID → 当前 session 参与者匹配 → trackId/athleteId`。

当前版本提供运动员检测与身份识别，不再提供实时姿态关键点、骨架、轨迹、动作评分或自动动作计数。历史训练记录中的旧姿态、评分和动作数据仍可读取与复盘；新训练只保存检测框、机位、trackId、身份状态和置信度。

系统同时支持 12 路完整帧率离线分析：Windows 端继续以默认每路 5 FPS 做低延迟实时预览；Ubuntu 24.04 + NVIDIA DeepStream 9 worker 对 NAS 中的 12 路 1080p60 同步录像逐解码帧执行 YOLO，并按轨迹触发 PersonViT。完整结果写入 NAS 的 10 秒 gzip JSONL 分块，PostgreSQL 只保存任务、版本、进度与分块索引。

## AI 模型

- `models/athlete/yolo26x.onnx`：官方 Ultralytics YOLO26x，`640x640`，端到端 NMS-free，输出 `300x6`，只接受 COCO `person` 类别。
- `models/athlete/personvit_msmt17_vit_base.onnx`：TransReID ViT-Base MSMT17 baseline，输入 `3x256x128`，RGB，均值/方差 `0.5`，输出 `768` 维 L2 归一化 embedding。
- 默认检测阈值 `0.35`、ReID 匹配阈值 `0.60`、候选差值 `0.05`、track TTL `1200 ms`。
- 二进制模型和 TensorRT engine 不提交 Git；模型来源、版本、shape 和 SHA256 见 `models/athlete/athlete_models.json` 与 `models/athlete/athlete_models.sha256`。

模型下载、YOLO 导出、PersonViT 转换和校验：

```powershell
.	ools\download_athlete_models.ps1
python tools\check_athlete_models.py
```

DeepStream 使用动态 batch 模型，需另外导出到被忽略的 `models/athlete/deepstream`：

```powershell
powershell -ExecutionPolicy Bypass -File tools/export_deepstream_models.ps1
```

## 数据服务

桌面端使用 Qt Widgets + QtNetwork，服务端使用 FastAPI，训练业务数据保存到 PostgreSQL。运动员管理页可以添加、查看和删除 ReID 样本，样本图片和 embedding 通过 `athlete_identity_samples` / `athlete_identity_embeddings` 保存。服务端样本文件目录由 `ISKATING_IDENTITY_GALLERY_ROOT` 配置。

历史页的“报告中心”可按训练保存日期、参与者、训练内时间段及专项指标导出 CSV 明细或 PDF 验收汇总。专项指标覆盖轨迹、速度、关节角和角速度；已有数据库需先运行 `python tools/backfill_joint_metrics.py` 后再启用关节指标保存。

```powershell
cd server
python -m venv .venv
.\.venv\Scripts\pip install -r requirements.txt
$env:ISKATING_DATABASE_URL="postgresql+psycopg://iskating:password@127.0.0.1:5432/iskating"
$env:ISKATING_JWT_SECRET="change-this"
$env:ISKATING_IDENTITY_GALLERY_ROOT="data/identity-gallery"
python ..\tools\reset_postgres_schema.py --yes
.\.venv\Scripts\uvicorn app.main:app --host 0.0.0.0 --port 8000
```

已有 PostgreSQL 数据库使用幂等回填工具增加轨迹速度与完整帧率协议；空库仍可直接重建：

```powershell
python tools/backfill_video_indexes.py
python tools/backfill_full_rate_analysis.py
```

## DeepStream 离线 worker

分析主机要求 Ubuntu 24.04、Docker、NVIDIA Container Toolkit、兼容 DeepStream 9 的驱动，并把同一 NAS 根目录挂载到容器 `/mnt/iskating`。FastAPI 与 worker 必须配置相同的工作令牌，并让各自的 `ISKATING_ANALYSIS_NAS_ROOT` 指向同一逻辑根目录。

```bash
export ISKATING_API_BASE_URL=http://api-host:8000
export ISKATING_ANALYSIS_WORKER_TOKEN='replace-with-a-long-random-token'
export ISKATING_ANALYSIS_WORKER_ID=deepstream-01
export ISKATING_ANALYSIS_NAS_ROOT=/srv/iskating
docker compose -f analysis_worker/compose.yml up -d --build
```

桌面端点击“完整分析”导入恰好 12 路 `nas://` 源，创建运行并查看每路进度。Windows 回放前需在该窗口配置同一 `nas://` 根目录对应的盘符或 UNC 路径；完成区间可渐进回放，缺口不会沿用旧检测框，新版本完成后需显式激活。

操作清单与 manifest 格式见 [12 路完整帧率分析用户指南](docs/full-rate-analysis-guide.md)。

桌面端默认连接 `http://127.0.0.1:8000`，可通过 `ISKATING_API_BASE_URL` 覆盖。模型缺失或 PersonViT 初始化失败时，视频播放仍可继续；对应身份识别能力会在状态栏提示不可用。

## 桌面端构建

需要 Qt 6.7.3 MSVC 2022 x64、FFmpeg shared dev package、TensorRT 10.1 和 CUDA 11.8：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
.\x64\Release\iskating.exe
```

Release 规则始终复制 `models/athlete` 的清单和校验文件；如果构建机已准备被忽略的 ONNX 二进制，也会一并复制到发布目录，否则需按下载脚本在发布机补齐。
