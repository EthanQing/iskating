# iSkating Coach

项目结构、运行方式和当前边界见 [项目概览](docs/ai-kb/01-project-overview.md) 与 [本地开发 Runbook](docs/ai-kb/runbooks/local-development.md)。

## F-24 二维滑行轨迹

完成每路相机的四点冰面标定后，应用将检测框底边投影到统一场地米制坐标，实时绘制并在保存训练时提交 `track_points`。坐标原点位于场地起点冰面，`+x` 指向滑行方向，`+y` 为预先约定的横向正方向，`z=0`。在系统设置的场地表双击“四点标定”单元格，输入四个像素点及其对应的场地 `(x,y)`；未标定的机位不会产生轨迹点。历史卡片的轨迹入口打开二维轨迹/速度面板，可导出 CSV/XLSX。

已知限制：客户端预生成的主运动员 participant UUID 与服务端重建的 UUID 可能不一致，导致主运动员轨迹/速度在入库时被跳过。实时绘制能力已存在，但持久化闭环需要修复后做 PostgreSQL 集成验证。

## F-25 滑行速度序列

每个有效轨迹点都关联一条 `speed_metrics` 记录，包含瞬时速度、1000ms 平滑速度、单位（`m/s`）、算法版本和有效标记。首点、时间异常、超过 2 秒的取样间隔或无效检测会保留为无效速度结果。历史轨迹复盘可按运动员筛选并查看、导出速度曲线；旧训练记录不会补写速度。

Windows Qt/C++ 滑冰训练辅助应用，当前实时 AI 主流程为：`YOLO26x person 检测 → PersonViT/MSMT17 ReID → 当前 session 参与者匹配 → trackId/athleteId`。

当前版本提供运动员检测、身份识别，以及在“已识别 + 有效四点标定”条件下的二维轨迹与速度；不再生成实时姿态关键点、3D 骨架、动作评分或自动动作计数。服务端仍保留历史姿态/评分/动作表和接口，客户端当前只复盘支持范围内的历史动作/评分数据，不再加载或绘制旧姿态关键点覆盖层；新训练保存检测框、机位、trackId、身份状态、置信度，并尝试保存有效轨迹/速度。

系统同时支持 12 路完整帧率离线分析：Windows 端继续以默认每路 5 FPS 做低延迟实时预览；Ubuntu 24.04 + NVIDIA DeepStream 9 worker 对 NAS 中的 12 路 1080p60 同步录像逐解码帧执行 YOLO，并按轨迹触发 PersonViT。完整结果写入 NAS 的 10 秒 gzip JSONL 分块，PostgreSQL 只保存任务、版本、进度与分块索引。

## AI 模型

- `models/athlete/yolo26x.onnx`：官方 Ultralytics YOLO26x，`640x640`，端到端 NMS-free，输出 `300x6`，只接受 COCO `person` 类别。
- `models/athlete/personvit_msmt17_vit_base.onnx`：TransReID ViT-Base MSMT17 baseline，输入 `3x256x128`，RGB，均值/方差 `0.5`，输出 `768` 维 L2 归一化 embedding。
- 默认检测阈值 `0.35`、ReID 匹配阈值 `0.60`、候选差值 `0.05`、track TTL `1200 ms`。
- 二进制模型和 TensorRT engine 不提交 Git；模型来源、版本、shape 和 SHA256 见 `models/athlete/athlete_models.json` 与 `models/athlete/athlete_models.sha256`。

模型下载、YOLO 导出、PersonViT 转换和校验：

```powershell
.\tools\download_athlete_models.ps1
python tools\check_athlete_models.py
```

DeepStream 使用动态 batch 模型，需另外导出到被忽略的 `models/athlete/deepstream`：

```powershell
powershell -ExecutionPolicy Bypass -File tools/export_deepstream_models.ps1
```

## 数据服务

桌面端使用 Qt Widgets + QtNetwork，服务端使用 FastAPI，训练业务数据保存到 PostgreSQL。运动员管理页可以添加、查看和删除 ReID 样本，样本图片和 embedding 通过 `athlete_identity_samples` / `athlete_identity_embeddings` 保存。服务端样本文件目录由 `ISKATING_IDENTITY_GALLERY_ROOT` 配置。

历史页的“报告中心”可按训练保存日期、参与者、训练内时间段及专项指标导出 CSV 明细或 PDF 验收汇总。专项指标覆盖轨迹、速度、关节角和角速度；已有数据库需先运行 `python tools/backfill_joint_metrics.py` 后再启用关节指标保存。当前历史顶部汇总和专项报告中心只使用客户端已加载的当前页 session，不代表全部筛选结果。

离线单视频导入和 12 路完整帧率批次会创建可追踪的通用分析任务；已有数据库可运行 `python tools/backfill_analysis_tasks.py` 补齐任务记录关联。
采集页的“任务中心”以单并发队列在后台准备本地导入、创建并提交完整帧率远端运行，再跟踪其进度；当前进程内可暂停、继续或取消。应用重启后会把遗留任务显示为 `paused`，但不会重建 job 参数，因此不能直接继续，需重新发起对应导入或批次。完整分析的“暂停”只停止客户端轮询，不会暂停远端 worker。

系统设置中的“连通测试”会逐路探测预览 RTSP 流，展示地址解析状态、UDP/TCP 协议、RTSP Open 耗时、首帧耗时、分辨率、帧率、失败阶段和错误码。结果仅供本次联调查看，不会保存或影响正在播放的视频。

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
python tools/backfill_analysis_tasks.py
python tools/backfill_joint_metrics.py
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

桌面端点击“完整分析”导入恰好 12 路 `nas://` 源，创建运行并查看每路进度。Windows 回放前需在该窗口配置同一 `nas://` 根目录对应的盘符或 UNC 路径；完成区间可渐进回放，缺口不会沿用旧检测框，新版本完成后需显式激活。完成或激活不会自动创建训练 session；运行中的模型/gallery 版本字段也只是标签，尚未冻结可复现快照。

操作清单与 manifest 格式见 [部署 Runbook](docs/ai-kb/runbooks/deployment.md) 和 [测试 Runbook](docs/ai-kb/runbooks/testing.md)。

桌面端默认连接 `http://127.0.0.1:8000`，可通过 `ISKATING_API_BASE_URL` 覆盖。模型缺失或 PersonViT 初始化失败时，视频播放仍可继续；对应身份识别能力会在状态栏提示不可用。

## 桌面端构建

需要 Qt 6.7.3 MSVC 2022 x64、FFmpeg shared dev package、TensorRT 10.1 和 CUDA 11.8：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
.\x64\Release\iskating.exe
```

Release 规则始终复制 `models/athlete` 的清单和校验文件；如果构建机已准备被忽略的 ONNX 二进制，也会一并复制到发布目录，否则需按下载脚本在发布机补齐。

## 项目结构

```text
iskating/
├─ mainwindow.pro              # qmake 入口
├─ build/qmake/                # 公共配置、依赖和部署规则
├─ src/
│  ├─ app/                     # 进程入口
│  ├─ ui/                      # Qt Widgets 与窗体
│  ├─ domain/                  # 训练领域数据结构
│  ├─ application/             # 训练流程与任务编排
│  └─ infrastructure/          # 视频、推理、持久化、配置
├─ resources/                  # qrc、图标、图片和 QSS
├─ tests/client/               # 客户端纯逻辑合同测试
├─ server/                     # FastAPI/PostgreSQL
├─ analysis_worker/            # DeepStream worker
└─ tools/                      # 模型与数据维护工具
```

新增 Qt/C++ 文件时，请放入对应 `src/` 分层并更新同层 `.pri`；不要把业务源码重新放回项目根目录。
