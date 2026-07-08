# iSkating Coach

Windows Qt/C++ 滑冰训练辅助应用，包含多路 RTSP/离线视频采集、人体姿态分析、动作评分、比赛归属、训练复盘和报告导出。

RTSP 视频接入支持 UDP 优先、TCP fallback 和断流自动重连；离线视频导入会先校验文件、视频轨、时长、seek 能力和 D3D11VA 硬解兼容性。界面会显示连接中、断流重连、长时间断流、编码/硬解不兼容等状态，日志中的 RTSP 密码会脱敏。

系统设置支持配置 NVR 回放模板，并可导入/导出 JSON 摄像头配置模板。模板包含公共 RTSP 参数、预览/主码流路径、NVR 回放模板、分析偏好和 12 路相机 IP/场地标定信息，便于现场批量落地；还可一键测试 12 路 RTSP 预览流连通性，查看成功/失败、UDP/TCP、分辨率、帧率和错误原因。应用内部仍使用 QSettings 保存本机配置并兼容旧字段。

## 数据服务

训练业务数据已切换为服务端架构：

- 桌面端：Qt Widgets + QtNetwork
- 服务端：FastAPI
- 数据库：PostgreSQL

训练记录可关联比赛基础信息（名称、地点、日期、类型、备注）、比赛场次/项目/轮次/分组和参赛运动员关系（参赛号、道次、成绩、名次），并自动标记分析结果归属为训练、比赛或导入视频。采集页支持最多 4 名 session 参与运动员和人工轨迹绑定，动作明细保存运动员身份、轨迹 ID、机位和帧时间。保存训练时会登记 session 级视频资产，按 `videoStorage/rootDir` 或默认本机数据目录生成可反查的录像目录、文件名和元数据路径；当前只保存规范化引用，不从 RTSP 实际录制文件。历史页支持统一比赛管理、按比赛/场次/来源筛选、跨 session 动作明细检索，并在报告导出中展示比赛归属、分析归属、身份轨迹信息和视频资产信息；动作明细可导出 CSV/XLSX。

桌面端默认连接 `http://127.0.0.1:8000`，可通过 `QSettings server/baseUrl` 或环境变量 `ISKATING_API_BASE_URL` 覆盖。默认开发登录可用 `ISKATING_API_USERNAME` / `ISKATING_API_PASSWORD` 覆盖。

## 后端启动

```powershell
cd server
python -m venv .venv
.\.venv\Scripts\pip install -r requirements.txt
$env:ISKATING_DATABASE_URL="postgresql+psycopg://iskating:password@127.0.0.1:5432/iskating"
$env:ISKATING_JWT_SECRET="change-this"
python ..\tools\reset_postgres_schema.py --yes
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
