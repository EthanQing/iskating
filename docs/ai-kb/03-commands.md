# Commands

上级入口：[[00-index|AI 知识库索引]]
相关 Runbook：[[runbooks/local-development|本地开发]]、[[runbooks/testing|测试]]、[[runbooks/deployment|部署]]、[[runbooks/debugging|调试]]
相关 Reference：[[references/environment-variables|环境变量]]、[[references/third-party-services|第三方服务]]
相关风险：[[05-pitfalls|坑点]]

## 安装依赖

桌面端没有统一包管理脚本，FastAPI 使用 `server/requirements.txt`，DeepStream worker 使用 Docker Compose。主要依赖包括：

- Qt 6.7.3 MSVC 2022 x64，默认路径见 `mainwindow.pro`，需要 QtNetwork。
- Python 3.11+、PostgreSQL 14+，用于 FastAPI 训练服务。
- Visual Studio 2022 MSVC x64 工具链，线索见 `.qmake.stash`。
- FFmpeg shared MSVC x64 开发包，默认路径见 `build/qmake/dependencies.pri`。
- TensorRT 10.1.0.27，默认路径见 `build/qmake/dependencies.pri`。
- CUDA 11.8，默认路径见 `build/qmake/dependencies.pri`。

服务端、worker 和桌面端的安装/运行示例见根目录 `README.md`、[[runbooks/local-development|本地开发 Runbook]] 和 [[runbooks/deployment|部署 Runbook]]。

## 本地开发

建议在 Visual Studio 2022 x64 Native Tools 环境中执行。默认构建 Release 版本：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
```

相关文件：

- `mainwindow.pro`
- `build/qmake/common.pri`
- `build/qmake/dependencies.pri`
- `build/qmake/deployment.pri`

## 构建

Debug/Release 输出目录由 `mainwindow.pro` 固定到 `x64/Debug` 和 `x64/Release`。除非明确需要调试符号和 Debug DLL，否则默认构建 `Release` 版本：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
```

需要 Debug 构建时再显式执行：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=debug"
nmake debug
```

注意：当前仓库里存在 `Makefile*`，但它们是 qmake 生成文件，已在 `.gitignore` 中标记为忽略。

## 运行

默认运行 Release：

```powershell
.\x64\Release\iskating.exe
```

Debug 构建产物：

```powershell
.\x64\Debug\iskating.exe
```

运行时依赖 DLL、`models/` 和 `plugins/platforms/qwindows(d).dll` 会由 `mainwindow.pro` 的 post-link 规则复制到输出目录。训练业务数据依赖外部 FastAPI/PostgreSQL 服务。

## 测试

客户端纯逻辑合同测试：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" tests/client/client-tests.pro "CONFIG+=release"
nmake release
.\x64\Release\tests\client-tests.exe
```

服务端和 worker 测试：

```powershell
uv run --python 3.12 --with-requirements server/requirements.txt python -m unittest discover -s server/tests -p "test_*.py"
```

## lint

TODO: 未在项目中找到 lint 命令。

## typecheck

C++ 项目没有独立 typecheck 命令；当前类型检查随 MSVC 编译发生。

TODO: 未找到单独的静态分析或 clang-tidy 配置。

## 数据库重建

开发期 PostgreSQL 数据库按空库重建处理。该命令会删除并重建当前业务表，只在可丢弃数据的开发库执行：

```powershell
$env:ISKATING_DATABASE_URL="postgresql+psycopg://iskating:password@127.0.0.1:5432/iskating"
python tools/reset_postgres_schema.py --yes
```

旧 SQLite 数据通过一次性工具导入：

```powershell
python tools/import_sqlite_to_postgres.py --sqlite "$env:APPDATA/iSkating/iSkating Coach/iskating.db"
```

已有 PostgreSQL 开发库保留数据并补齐视频索引字段：

```powershell
python tools/backfill_video_indexes.py
```

完整帧率分析 schema 幂等回填：

```powershell
python tools/backfill_full_rate_analysis.py
```

已有开发库还可分别回填通用分析任务和关节指标：

```powershell
python tools/backfill_analysis_tasks.py
python tools/backfill_joint_metrics.py
```

回填脚本用于需要保留数据的已有开发库；可丢弃的空库优先直接重建当前 schema。

## seed 数据

FastAPI 服务启动时会 seed 默认管理员、默认运动员、默认教练、8 个动作类别和 8 条动作标准。

## 模型下载与校验

YOLO26x 和 TransReID MSMT17 PersonViT 可通过脚本下载、转换：

```powershell
powershell -ExecutionPolicy Bypass -File tools/download_athlete_models.ps1
python tools/check_athlete_models.py
```

DeepStream 动态 batch 模型导出：

```powershell
powershell -ExecutionPolicy Bypass -File tools/export_deepstream_models.ps1
```

Ubuntu DeepStream worker：

```bash
docker compose -f analysis_worker/compose.yml config
docker compose -f analysis_worker/compose.yml up -d --build
```

相关文件：

- `tools/download_athlete_models.ps1`
- `tools/convert_personvit_msmt17.py`
- `tools/check_athlete_models.py`
- `models/athlete/athlete_models.json`
- `models/athlete/athlete_models.sha256`

## 部署相关命令

Release 构建后，`build/qmake/deployment.pri` 会调用：

- `windeployqt.exe --release --no-translations`
- 复制 FFmpeg DLL
- 复制 TensorRT/CUDA DLL
- 复制 `models/athlete` 的模型清单和校验文件

TODO: 未找到打包安装器、发布脚本或 CI/CD 发布命令。
