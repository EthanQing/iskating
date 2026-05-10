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

    // 启动时使用系统“最大化”状态显示主界面；这不是 F11 全屏，仍保留标题栏和任务栏。
    window.showMaximized();

    return app.exec();
}
