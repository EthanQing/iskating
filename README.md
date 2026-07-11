# iSkating Coach

Windows Qt/C++ 滑冰训练辅助应用，当前实时 AI 主流程为：`YOLO26x person 检测 → PersonViT/MSMT17 ReID → 当前 session 参与者匹配 → trackId/athleteId`。

当前版本提供运动员检测与身份识别，不再提供实时姿态关键点、骨架、轨迹、动作评分或自动动作计数。历史训练记录中的旧姿态、评分和动作数据仍可读取与复盘；新训练只保存检测框、机位、trackId、身份状态和置信度。

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

## 数据服务

桌面端使用 Qt Widgets + QtNetwork，服务端使用 FastAPI，训练业务数据保存到 PostgreSQL。运动员管理页可以添加、查看和删除 ReID 样本，样本图片和 embedding 通过 `athlete_identity_samples` / `athlete_identity_embeddings` 保存。服务端样本文件目录由 `ISKATING_IDENTITY_GALLERY_ROOT` 配置。

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

桌面端默认连接 `http://127.0.0.1:8000`，可通过 `ISKATING_API_BASE_URL` 覆盖。模型缺失或 PersonViT 初始化失败时，视频播放仍可继续；对应身份识别能力会在状态栏提示不可用。

## 桌面端构建

需要 Qt 6.7.3 MSVC 2022 x64、FFmpeg shared dev package、TensorRT 10.1 和 CUDA 11.8：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
.\x64\Release\iskating.exe
```

Release 规则始终复制 `models/athlete` 的清单和校验文件；如果构建机已准备被忽略的 ONNX 二进制，也会一并复制到发布目录，否则需按下载脚本在发布机补齐。
