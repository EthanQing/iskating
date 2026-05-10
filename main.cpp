#include "mainwindow.h"

#include <QApplication>
#include <QRect>
#include <QScreen>
#include <QSize>

#include <algorithm>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("iSkating Coach"));
    QApplication::setOrganizationName(QStringLiteral("iSkating"));

    MainWindow window;

    const QRect availableGeometry = QApplication::primaryScreen()
                                        ? QApplication::primaryScreen()->availableGeometry()
                                        : QRect(0, 0, 1280, 720);
    const QSize preferredSize(1280, 820);
    const QSize maxInitialSize(std::max(1, static_cast<int>(availableGeometry.width() * 0.92)),
                               std::max(1, static_cast<int>(availableGeometry.height() * 0.92)));
    const QSize initialSize(std::min(preferredSize.width(), maxInitialSize.width()),
                            std::min(preferredSize.height(), maxInitialSize.height()));

    window.resize(initialSize);
    window.move(availableGeometry.center() - window.rect().center());
    window.show();

    return app.exec();
}
