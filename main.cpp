#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

QStringList existingDirectories(const QStringList &paths)
{
    QStringList existing;
    for (const QString &path : paths) {
        if (QDir(path).exists() && !existing.contains(path)) {
            existing.append(path);
        }
    }
    return existing;
}

QStringList localPluginSearchPaths(const QString &appDir)
{
    QStringList paths;
    paths.append(appDir);
    paths.append(QDir(appDir).absoluteFilePath(QStringLiteral("plugins")));
    return existingDirectories(paths);
}

QStringList localRuntimeSearchPaths(const QString &appDir)
{
    const QString pluginDir = QDir(appDir).absoluteFilePath(QStringLiteral("plugins"));

    QStringList paths;
    paths.append(appDir);
    paths.append(QDir(appDir).absoluteFilePath(QStringLiteral("multimedia")));
    paths.append(pluginDir);
    paths.append(QDir(pluginDir).absoluteFilePath(QStringLiteral("multimedia")));
    paths.append(QDir(appDir).absoluteFilePath(QStringLiteral("bin")));
    return paths;
}

QByteArray pathListToLocal8Bit(const QStringList &paths)
{
#ifdef Q_OS_WIN
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif

    QStringList nativePaths;
    for (const QString &path : paths) {
        nativePaths.append(QDir::toNativeSeparators(path));
    }
    return nativePaths.join(QLatin1Char(separator)).toLocal8Bit();
}

void prependEnvironmentPath(const char *name, const QStringList &paths)
{
    const QStringList existing = existingDirectories(paths);
    if (existing.isEmpty()) {
        return;
    }

    QByteArray value = pathListToLocal8Bit(existing);
    const QByteArray oldValue = qgetenv(name);
    if (!oldValue.isEmpty()) {
#ifdef Q_OS_WIN
        value += ';';
#else
        value += ':';
#endif
        value += oldValue;
    }
    qputenv(name, value);
}

#ifdef Q_OS_WIN
void configureWindowsDllDirectory(const QString &appDir)
{
    // 让 LoadLibrary 优先在 exe 当前目录查找插件依赖库，例如 FFmpeg 的 avcodec/avformat。
    // 其它子目录通过 PATH 前置补充。
    const QString nativeAppDir = QDir::toNativeSeparators(appDir);
    SetDllDirectory(reinterpret_cast<LPCWSTR>(nativeAppDir.utf16()));
}
#endif

} // namespace

int main(int argc, char *argv[])
{
#ifdef QT_DEBUG_PLUGINS
    // qmake 中定义 QT_DEBUG_PLUGINS 后，启动阶段同步打开 Qt 插件加载诊断。
    // 注意 Qt 真正读取的是环境变量 QT_DEBUG_PLUGINS=1，不只是 C++ 宏。
    qputenv("QT_DEBUG_PLUGINS", "1");
    qputenv("QT_FFMPEG_DEBUG", "1");
    qputenv("QT_LOGGING_RULES", "qt.multimedia.*=true;qt.plugin.*=true");
#endif

    // FFmpeg 后端打开 RTSP 时会继续使用 rtp/udp/tcp 等子协议；显式放行，避免
    // 插件已加载但 RTSP 子协议被安全白名单挡住导致只进入 PlayingState 却没有帧。
    if (qEnvironmentVariableIsEmpty("QT_FFMPEG_PROTOCOL_WHITELIST")) {
        qputenv("QT_FFMPEG_PROTOCOL_WHITELIST", "file,crypto,data,rtp,udp,tcp,tls,http,https,httpproxy,rtsp");
    }

    // 强制 Qt Multimedia 只使用 FFmpeg 后端。不要在 FFmpeg 不存在时回退到
    // windowsmediaplugin；否则 RTSP 认证流仍可能出现 0x80070005 或无画面。
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");

    const QString startupAppDir = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
    const QString localPluginSubdir = QDir(startupAppDir).absoluteFilePath(QStringLiteral("plugins"));
    const QStringList localPluginPaths = localPluginSearchPaths(startupAppDir);
    const QStringList localRuntimePaths = localRuntimeSearchPaths(startupAppDir);

    // 插件和插件依赖库只优先从程序本地目录查找，避免运行时跑到 C:/OSGeo4W 下加载。
    if (!localPluginPaths.isEmpty()) {
        qputenv("QT_PLUGIN_PATH", pathListToLocal8Bit(localPluginPaths));
        // QApplication 构造时就会加载 platforms/qwindows.dll，因此在构造前先限定一次。
        QCoreApplication::setLibraryPaths(localPluginPaths);
    }
    prependEnvironmentPath("PATH", localRuntimePaths);
#ifdef Q_OS_WIN
    configureWindowsDllDirectory(startupAppDir);
#endif

    const bool ffmpegBackendAvailable =
        QFileInfo(startupAppDir + QStringLiteral("/multimedia/ffmpegmediaplugin.dll")).exists()
        || QFileInfo(localPluginSubdir + QStringLiteral("/multimedia/ffmpegmediaplugin.dll")).exists();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("iSkating Coach"));
    QApplication::setOrganizationName(QStringLiteral("iSkating"));

    // QApplication 构造后再次限定 Qt 插件根目录；multimedia/platforms/imageformats
    // 等插件会从这些本地根目录的子目录加载。
    QApplication::setLibraryPaths(localPluginPaths);
    qDebug() << "[main] Qt library paths:" << QApplication::libraryPaths();
    qDebug() << "[main] QT_PLUGIN_PATH:" << qEnvironmentVariable("QT_PLUGIN_PATH");
    qDebug() << "[main] QT_DEBUG_PLUGINS:" << qEnvironmentVariable("QT_DEBUG_PLUGINS");
    qDebug() << "[main] QT_MEDIA_BACKEND:" << qEnvironmentVariable("QT_MEDIA_BACKEND");
    qDebug() << "[main] QT_FFMPEG_DEBUG:" << qEnvironmentVariable("QT_FFMPEG_DEBUG");
    qDebug() << "[main] QT_FFMPEG_PROTOCOL_WHITELIST:" << qEnvironmentVariable("QT_FFMPEG_PROTOCOL_WHITELIST");
    qDebug() << "[main] QT_LOGGING_RULES:" << qEnvironmentVariable("QT_LOGGING_RULES");
    qDebug() << "[main] local windows multimedia plugin exists:"
             << QFileInfo(QCoreApplication::applicationDirPath() + QStringLiteral("/multimedia/windowsmediaplugin.dll")).exists()
             << QCoreApplication::applicationDirPath() + QStringLiteral("/multimedia/windowsmediaplugin.dll");
    qDebug() << "[main] local ffmpeg multimedia plugin exists:"
             << QFileInfo(QCoreApplication::applicationDirPath() + QStringLiteral("/multimedia/ffmpegmediaplugin.dll")).exists()
             << QCoreApplication::applicationDirPath() + QStringLiteral("/multimedia/ffmpegmediaplugin.dll");
    qDebug() << "[main] local plugin-root ffmpeg multimedia plugin exists:"
             << QFileInfo(localPluginSubdir + QStringLiteral("/multimedia/ffmpegmediaplugin.dll")).exists()
             << localPluginSubdir + QStringLiteral("/multimedia/ffmpegmediaplugin.dll");
    if (!ffmpegBackendAvailable) {
        qDebug() << "[main] FFmpeg backend is forced, but ffmpegmediaplugin.dll was not found in local plugin paths;"
                 << "Qt Multimedia will not fall back to windowsmediaplugin. Deploy multimedia/ffmpegmediaplugin.dll and its FFmpeg runtime DLLs.";
    }

    MainWindow window;

    // 启动时直接进入与 F11 一致的全屏状态；Esc/F11 退出后恢复为最大化窗口。
    window.setProperty("previousWindowStateBeforeFullScreen", static_cast<int>(Qt::WindowMaximized));
    window.showFullScreen();

    return app.exec();
}
