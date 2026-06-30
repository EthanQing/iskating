# iSkating Coach

Windows Qt/C++ 滑冰训练辅助应用，包含多路 RTSP/离线视频采集、人体姿态分析、动作评分、训练复盘和报告导出。

## 数据服务

训练业务数据已切换为服务端架构：

- 桌面端：Qt Widgets + QtNetwork
- 服务端：FastAPI
- 数据库：PostgreSQL

桌面端默认连接 `http://127.0.0.1:8000`，可通过 `QSettings server/baseUrl` 或环境变量 `ISKATING_API_BASE_URL` 覆盖。默认开发登录可用 `ISKATING_API_USERNAME` / `ISKATING_API_PASSWORD` 覆盖。

## 后端启动

```powershell
cd server
python -m venv .venv
.\.venv\Scripts\pip install -r requirements.txt
$env:ISKATING_DATABASE_URL="postgresql+psycopg://iskating:password@127.0.0.1:5432/iskating"
$env:ISKATING_JWT_SECRET="change-this"
.\.venv\Scripts\alembic upgrade head
.\.venv\Scripts\uvicorn app.main:app --host 0.0.0.0 --port 8000
```

旧 SQLite 数据可在切换前导入：

```powershell
python tools/import_sqlite_to_postgres.py --sqlite "$env:APPDATA/iSkating/iSkating Coach/iskating.db"
```

## 桌面端构建

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
.\x64\Release\iskating.exe
```
