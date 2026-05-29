# Commands

上级入口：[[00-index|AI 知识库索引]]
相关 Runbook：[[runbooks/local-development|本地开发]]、[[runbooks/testing|测试]]、[[runbooks/deployment|部署]]、[[runbooks/debugging|调试]]
相关 Reference：[[references/environment-variables|环境变量]]、[[references/third-party-services|第三方服务]]
相关风险：[[05-pitfalls|坑点]]

## 安装依赖

项目没有包管理器脚本。依赖需要本机预先安装：

- Qt 6.7.3 MSVC 2022 x64，默认路径见 `mainwindow.pro`，需要 QtSql 和 SQLite driver。
- Visual Studio 2022 MSVC x64 工具链，线索见 `.qmake.stash`。
- FFmpeg shared MSVC x64 开发包，默认路径见 `mainwindow.pro`。
- TensorRT 10.1.0.27，默认路径见 `mainwindow.pro`。
- CUDA 11.8，默认路径见 `mainwindow.pro`。

TODO: 未找到正式依赖安装文档或自动化安装脚本。

## 本地开发

建议在 Visual Studio 2022 x64 Native Tools 环境中执行：

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro
nmake
```

相关文件：

- `mainwindow.pro`
- `common.pri`

## 构建

Debug/Release 输出目录由 `mainwindow.pro` 固定到 `x64/Debug` 和 `x64/Release`。默认构建 `Release` 版本

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=debug"
nmake debug
```

```powershell
& "C:/Qt/6.7.3/msvc2022_64/bin/qmake.exe" mainwindow.pro "CONFIG+=release"
nmake release
```

注意：当前仓库里存在 `Makefile*`，但它们是 qmake 生成文件，已在 `.gitignore` 中标记为忽略。

## 运行

```powershell
.\x64\Debug\iskating.exe
```

```powershell
.\x64\Release\iskating.exe
```

运行时依赖 DLL、`models/`、`plugins/platforms/qwindows(d).dll` 和 `plugins/sqldrivers/qsqlite(d).dll` 会由 `mainwindow.pro` 的 post-link 规则复制到输出目录。

## 测试

TODO: 未在项目中找到单元测试、集成测试或 E2E 测试命令。

## lint

TODO: 未在项目中找到 lint 命令。

## typecheck

C++ 项目没有独立 typecheck 命令；当前类型检查随 MSVC 编译发生。

TODO: 未找到单独的静态分析或 clang-tidy 配置。

## 数据库迁移

没有独立迁移命令。应用启动时 `TrainingRepository::open()` 会创建或升级 `%APPDATA%/iSkating/iSkating Coach/iskating.db`，并迁移旧 `QSettings/trainingHistory`。

## seed 数据

没有独立 seed 命令。应用启动时会 seed 默认运动员、默认教练、8 个动作类别和 8 条动作标准。

## 模型下载

RTMW3D-x ONNX 可通过脚本下载：

```powershell
powershell -ExecutionPolicy Bypass -File tools/download_rtmw3d_x.ps1
```

相关文件：

- `tools/download_rtmw3d_x.ps1`
- `models/body/body_model.json`

## 部署相关命令

Release 构建后，`mainwindow.pro` 会调用：

- `windeployqt.exe --release --no-translations`
- 复制 FFmpeg DLL
- 复制 TensorRT/CUDA DLL
- 复制 `models/`

TODO: 未找到打包安装器、发布脚本或 CI/CD 发布命令。
