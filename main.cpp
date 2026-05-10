#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("iSkating Coach"));
    QApplication::setOrganizationName(QStringLiteral("iSkating"));

    const QString osgeoQtPluginPath = QStringLiteral("C:/OSGeo4W/apps/Qt6/plugins");
    if (QDir(osgeoQtPluginPath).exists()) {
        QApplication::addLibraryPath(osgeoQtPluginPath);
    }
    qDebug() << "[main] Qt library paths:" << QApplication::libraryPaths();
    qDebug() << "[main] multimedia plugin exists:"
             << QFileInfo(osgeoQtPluginPath + QStringLiteral("/multimedia/windowsmediaplugin.dll")).exists()
             << osgeoQtPluginPath + QStringLiteral("/multimedia/windowsmediaplugin.dll");

    MainWindow window;

    // 启动时直接进入与 F11 一致的全屏状态；Esc/F11 退出后恢复为最大化窗口。
    window.setProperty("previousWindowStateBeforeFullScreen", static_cast<int>(Qt::WindowMaximized));
    window.showFullScreen();

    return app.exec();
}
