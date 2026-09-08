# iSkating Coach

Windows 滑冰训练辅助桌面应用，使用 Qt Widgets / C++20。支持多机位视频预览、运动员检测与身份识别、训练记录和历史复盘；完成场地标定后可生成二维轨迹与速度。当前新训练不生成自动动作计数、技术评分或 3D 骨架。

## Windows 构建与运行

### 环境要求

- Visual Studio 2022，安装“使用 C++ 的桌面开发”（MSVC x64、Windows SDK）。
- Qt 6.7.3，MSVC 2022 64 位版本。
- FFmpeg 8.0.1 shared 开发包，包含 `include`、`lib`、`bin`。
- TensorRT 10.1、CUDA Toolkit 11.8；实时 AI 运行还需要兼容的 NVIDIA GPU 和驱动。

当前项目使用以下默认路径：

| 依赖 | 默认路径 | 修改方式 |
| --- | --- | --- |
| Qt | `C:/Qt/6.7.3/msvc2022_64` | `mainwindow.pro` 中的 `QT_ROOT` |
| FFmpeg | `C:/Users/qc/zm/ffmpeg-8.0.1-full_build-shared` | 环境变量 `FFMPEG_ROOT` |
| TensorRT | `C:/Program Files/TensorRT-10.1.0.27` | 环境变量 `TENSORRT_ROOT` |
| CUDA | `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8` | 环境变量 `CUDA_ROOT` |

依赖路径和部署规则分别在 `build/qmake/dependencies.pri`、`build/qmake/deployment.pri` 中。部署规则使用指定版本的 DLL 文件名，不能只改目录就任意替换依赖版本。

### 构建 Release

1. **先退出正在运行的 iSkating**，否则构建可能无法覆盖 EXE 或 DLL。
2. 从开始菜单打开 **x64 Native Tools Command Prompt for VS 2022**。
3. 执行下面命令；仓库位置不同时替换第一行路径。

```bat
cd /d C:\Users\qc\Desktop\deep-thought\iskating
C:\Qt\6.7.3\msvc2022_64\bin\qmake.exe mainwindow.pro "CONFIG+=release"
nmake /nologo /f Makefile.Release
```

等待命令成功结束后运行：

```bat
x64\Release\iskating.exe
```

输出目录为 `x64/Release`。构建会复制运行依赖、Qt 插件和模型配置，运行时应保留整个输出目录，不要只复制 EXE。

后续只修改 C++ 源码时，可以在同一开发者命令提示符中直接执行 `nmake /nologo /f Makefile.Release` 增量构建。修改 `.pro`、`.pri`、依赖路径或新增/删除源码后，先重新执行 qmake。

如果使用普通 CMD，可先执行下面命令初始化本机 Community 版 MSVC 环境，再执行上述构建命令：

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
```

普通 PowerShell 中直接运行这个 `.bat` 不会把环境保留到 PowerShell；推荐使用上述开发者命令提示符。

## 首次运行与配置

- “系统状态”的训练服务区域复用启动时的连接结果与实际检查完成时间：连接成功显示 API“已连接”、数据库“正常”；失败显示 API“未连接”、数据库“未知”，具体错误可悬停查看。切换页面不会重新检查；AI 分析、摄像头和本机资源仍以“—”表示未检测。
- “系统设置”配置公共 RTSP 参数、摄像头 IP、场地标定、AI 和存储选项。
- 桌面端通过 FastAPI 访问训练数据，不直接连接 PostgreSQL。默认服务地址为 `http://127.0.0.1:8000`，可用环境变量 `ISKATING_API_BASE_URL` 覆盖。
- 保存训练、人员管理和历史查询需要可用的训练服务及数据库。顶部“服务未连接”应检查服务是否启动及地址是否正确。
- AI 模型放在 `models/athlete/`，契约见 `models/athlete/athlete_models.json`。ONNX 模型和 TensorRT engine 不随 Git 提交。构建时会复制已有 ONNX；缺少模型时需先准备模型，再重新构建。

模型准备入口（仓库根目录，PowerShell）：

```powershell
.\tools\download_athlete_models.ps1
python tools\check_athlete_models.py
```

## 训练服务

服务使用 Python / FastAPI / PostgreSQL，依赖清单为 `server/requirements.txt`。以下命令用于启动服务，前提是已准备好 PostgreSQL 数据库及项目表结构；示例账号、密码和密钥需要替换。

在仓库根目录打开 PowerShell：

```powershell
python -m venv server/.venv
.\server\.venv\Scripts\python.exe -m pip install -r server/requirements.txt
$env:ISKATING_DATABASE_URL = "postgresql+psycopg://iskating:YOUR_PASSWORD@127.0.0.1:5432/iskating"
$env:ISKATING_JWT_SECRET = "REPLACE_WITH_A_LONG_RANDOM_SECRET"
.\server\.venv\Scripts\python.exe -m uvicorn app.main:app --app-dir server --host 127.0.0.1 --port 8000
```

数据库表结构定义在 `server/app/schema.py`。`tools/reset_postgres_schema.py --yes` 会重建并破坏已有数据，不是日常启动或升级命令。

12 路完整帧率分析另有 Ubuntu / NVIDIA DeepStream worker，入口为 `analysis_worker/compose.yml`；普通桌面端构建不需要启动它。

## 常见构建问题

- **找不到 nmake / cl**：使用 x64 Native Tools Command Prompt，或先在 CMD 中调用 `vcvars64.bat`。
- **文件正由另一进程使用 / 无法复制 DLL / 无法写入 EXE**：退出 iSkating 和调试会话后重新构建。
- **qmake 提示缺少头文件或 .lib**：核对上述 SDK 路径，FFmpeg 必须包含开发头文件与 MSVC 导入库。
- **修改后仍显示旧界面**：确认构建成功，并运行对应的 `x64/Release/iskating.exe`；仅编译 `.obj` 不会更新 EXE。

## 主要目录

| 路径 | 用途 |
| --- | --- |
| `mainwindow.pro`、`build/qmake/` | qmake 构建入口、依赖和部署规则 |
| `src/ui/`、`resources/` | 桌面界面、QSS、图标和资源 |
| `src/domain/`、`src/application/` | 领域模型与训练流程 |
| `src/infrastructure/` | 视频、推理、服务访问和本机配置 |
| `server/` | 训练 API 与数据库表结构 |
| `analysis_worker/` | DeepStream 完整帧率分析 worker |
| `models/athlete/`、`tools/` | 模型配置和维护工具 |
| `tests/client/` | 客户端合同测试 |

本地修改完成后提交 Git；仅在明确需要发布时推送远端。
