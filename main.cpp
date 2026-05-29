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
    QStringList paths;
    paths.append(appDir);
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
    // 让 LoadLibrary 优先在 exe 当前目录查找 FFmpeg/Qt 运行库。
    const QString nativeAppDir = QDir::toNativeSeparators(appDir);
    SetDllDirectory(reinterpret_cast<LPCWSTR>(nativeAppDir.utf16()));
}
#endif

} // namespace

int main(int argc, char *argv[])
{
    const QString startupAppDir = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
    const QStringList localPluginPaths = localPluginSearchPaths(startupAppDir);
    const QStringList localRuntimePaths = localRuntimeSearchPaths(startupAppDir);

    // Qt 插件和 FFmpeg/Qt 运行库只优先从程序本地目录查找。
    if (!localPluginPaths.isEmpty()) {
        qputenv("QT_PLUGIN_PATH", pathListToLocal8Bit(localPluginPaths));
        // QApplication 构造时就会加载 platforms/qwindows.dll，因此在构造前先限定一次。
        QCoreApplication::setLibraryPaths(localPluginPaths);
    }
    prependEnvironmentPath("PATH", localRuntimePaths);
#ifdef Q_OS_WIN
    configureWindowsDllDirectory(startupAppDir);
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("iSkating Coach"));
    QApplication::setOrganizationName(QStringLiteral("iSkating"));

    // QApplication 构造后再次限定 Qt 插件根目录；platforms/imageformats 等插件会从这里加载。
    QApplication::setLibraryPaths(localPluginPaths);
    qDebug() << "[main] Qt library paths:" << QApplication::libraryPaths();
    qDebug() << "[main] QT_PLUGIN_PATH:" << qEnvironmentVariable("QT_PLUGIN_PATH");
    qDebug() << "[main] local FFmpeg runtime exists:"
             << QFileInfo(QCoreApplication::applicationDirPath() + QStringLiteral("/avcodec-62.dll")).exists()
             << QCoreApplication::applicationDirPath();

    MainWindow window;

    // 启动时使用最大化窗口，保留 F11 手动进入全屏。
    window.setProperty("previousWindowStateBeforeFullScreen", static_cast<int>(Qt::WindowMaximized));
    window.showMaximized();

    return app.exec();
}
