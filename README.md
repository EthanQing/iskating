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

- “系统状态”的训练服务区域复用启动时的连接结果与实际检查完成时间：连接成功显示 API“已连接”、数据库“正常”；失败显示 API“未连接”、数据库“未知”，具体错误可悬停查看。切换页面不会重新检查。
- 系统状态页的“执行检查 / 重新检查”由用户触发一次检查：读取当前服务、AI 和分析任务缓存，刷新本机视频存储状态，并用 CameraConnectivityTester 在临时线程中逐路探测已保存的摄像头配置。进度分母仅包含已配置槽位，不初始化服务或模型、不扫描视频资产、不替换训练中的视频流。可以切换页面等待完成。
- 主动摄像头结果优先于被动状态，在线显示成功数 / 已配置数，异常显示失败数；未配置槽位跳过，全部未配置时在线和异常均为“—”。ToolTip 提供时间、统计和逐路结果，隐藏密码。结果仅保留在当前进程；保存系统设置会使旧结果失效，并取消仍在进行的旧配置检查，等待再次手动检查。
- “最近检查”启动时记录服务初始化完成时间，主动检查完成后记录环境检查结束时间，不代表重新执行了数据库 Ping。退出程序时协作中断检查并等待临时线程释放。
- AI 分析区域复用现有初始化状态，分别展示 Person 检测和身份识别能力；ReID 降级不会将已就绪的 Person 检测标记为不可用，等待视频帧也不代表模型故障。模型状态的完整说明可悬停查看。
- 摄像头“已配置”按非空 IP 显示配置数量 / 槽位总数，启动加载和系统设置保存后同步更新。在线与异常复用现有视频流：Playing 计在线，Error 计异常，连接中和重连中仅在 ToolTip 中统计；没有现存 Stream 时显示“未检测 / —”。每路优先读取预览 Stream，缺失时读取该路已选中的主画面 Stream，不重复计数，也不计入离线视频或未配置槽位。
- 视频存储在启动加载和系统设置保存后检查已配置目录与所在卷，显示“可用”“未配置”或“不可用”；路径和磁盘容量在 ToolTip 中查看，不扫描视频资产。
- “分析任务”直接统计 AnalysisTaskManager 缓存：运行中与排队合计为“进行中”，无活动任务时显示暂停数量或“空闲”；历史完成、失败和取消只在 ToolTip 中统计。管理器未创建显示“未就绪”，服务未连接且缓存为空显示“未同步”。任务信号即时刷新，摄像头复用现有每秒状态刷新；这些被动状态不新增网络请求、RTSP 探测或定时器，也不启动视频流。
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

本地测试也可双击仓库根目录的 `start-backend.cmd`：先将 `server/local-env.cmd.example` 复制为 `server/local-env.cmd`，填入测试数据库连接。此本地配置已被 Git 忽略；已有环境变量也可直接使用。脚本从仓库根目录运行，复用或创建 `server/.venv`，缺少依赖时安装，随后在 `127.0.0.1:8000` 启动服务并监视 `server/app` 自动重载。按 Ctrl+C 停止；启动错误会保留在窗口中。未配置 `ISKATING_JWT_SECRET` 时，每次启动生成随机密钥，因此重启后需要重新登录。脚本不创建或重置数据库表，服务仍执行现有的默认数据初始化逻辑。

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

视频画面和覆盖层的原生窗口应限制在 VideoOpenGLWidget 内，避免将外层页面、分隔区和侧栏提升为原生窗口，增加布局动画的窗口调整开销。尚未创建交换链时，清空画面不初始化图形资源；实际视频帧到达后再创建渲染资源。

界面颜色以 `resources/styles/iskating.qss` 为主：应用背景、普通面板、弹窗和交互控件分别使用 Background / Surface / Elevated / Interactive 层级。`AnimatedButton` 与轨迹手工绘制使用相同语义配色；普通文字操作使用 `secondary` 或 `subtle`，`ghost` 仅用于次要工具动作。FramelessDialog 和系统设置的结构样式统一放在全局 QSS，不再覆盖一套本地控件主题。

人员管理复用 FramelessDialog，以运动员 / 教练切换、左侧档案列表和右侧可滚动详情组织内容，保存与归档操作固定在详情底部。搜索只过滤已加载的人员：运动员匹配姓名、编号，教练另匹配专项，不发起搜索请求。人员归档后退出训练选择，历史记录保留；ReID 样本仍使用原有 PersonViT 提取与保存流程，未保存运动员前不能添加样本。训练服务未连接时禁用编辑及所有写入操作。

当前深色 Design System 已完成视觉收尾并冻结。后续界面复用现有 Palette、按钮状态与导航层级，不再进行配色微调；只有用户明确要求时才重新开启视觉调整。

`FramelessDialog` 自动安装模态遮罩；普通管理弹窗可通过 `installModalScrim()` 复用。遮罩仅在弹窗实际模态显示时出现，使用独立透明工具窗口覆盖原生视频子窗口，随所属窗口移动、缩放，并在弹窗隐藏或销毁时清理。非模态训练设置 Drawer 不显示遮罩；不要将工作区提升为原生窗口，也不要为遮罩加入截图、模糊或图形特效。
