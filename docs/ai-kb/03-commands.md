# 命令速查

[返回索引](00-index.md) · [本地开发](runbooks/local-development.md) · [测试](runbooks/testing.md) · [部署](runbooks/deployment.md)

> 默认在仓库根目录执行。危险命令已单独标注。

## Qt 客户端

在 Visual Studio 2022 x64 Native Tools 环境：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
.\x64\Release\iskating.exe
```

Debug：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=debug"
nmake debug
.\x64\Debug\iskating.exe
```

普通 PowerShell 无 `nmake` 时：

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && nmake release"
```

## 客户端合同测试

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" tests/client/client-tests.pro "CONFIG+=release"
nmake release
.\x64\Release\tests\client-tests.exe
```

## FastAPI 服务

```powershell
cd server
python -m venv .venv
.\.venv\Scripts\python -m pip install -r requirements.txt
$env:ISKATING_DATABASE_URL="postgresql+psycopg://iskating:password@127.0.0.1:5432/iskating"
$env:ISKATING_JWT_SECRET="replace-with-a-development-secret"
.\.venv\Scripts\uvicorn app.main:app --host 127.0.0.1 --port 8000
```

健康检查：

```powershell
Invoke-RestMethod http://127.0.0.1:8000/health
```

## Python 测试

使用当前环境：

```powershell
python -m unittest discover -s server/tests -p "test_*.py"
```

无需预建环境时可使用项目现有文档约定的 `uv`：

```powershell
uv run --python 3.12 --with-requirements server/requirements.txt python -m unittest discover -s server/tests -p "test_*.py"
```

单文件示例：

```powershell
python -m unittest server.tests.test_analysis_artifacts
```

## PostgreSQL schema 与回填

**危险：以下 reset 会删除并重建业务表，只能用于确认可丢弃的开发库。**

```powershell
$env:ISKATING_DATABASE_URL="postgresql+psycopg://iskating:password@127.0.0.1:5432/iskating"
python tools/reset_postgres_schema.py --yes
```

已有开发库按需要执行幂等回填：

```powershell
python tools/backfill_video_indexes.py
python tools/backfill_full_rate_analysis.py
python tools/backfill_analysis_tasks.py
python tools/backfill_joint_metrics.py
```

旧 SQLite 一次性导入：

```powershell
python tools/import_sqlite_to_postgres.py --sqlite "$env:APPDATA/iSkating/iSkating Coach/iskating.db"
```

## 模型

```powershell
powershell -ExecutionPolicy Bypass -File tools/download_athlete_models.ps1
python tools/check_athlete_models.py
powershell -ExecutionPolicy Bypass -File tools/export_deepstream_models.ps1
```

## DeepStream worker

```bash
export ISKATING_API_BASE_URL=http://api-host:8000
export ISKATING_ANALYSIS_WORKER_TOKEN='replace-with-a-long-random-token'
export ISKATING_ANALYSIS_WORKER_ID=deepstream-01
export ISKATING_ANALYSIS_NAS_ROOT=/srv/iskating
docker compose -f analysis_worker/compose.yml config
docker compose -f analysis_worker/compose.yml up -d --build
```

## 当前缺失

仓库没有已确认的 lint、clang-tidy、类型检查、CI/CD 或安装器命令。C++ 类型检查主要随 MSVC 构建发生。
