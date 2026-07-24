#-------------------------------------------------
# iSkating Coach Qt Widgets interface
#-------------------------------------------------

TARGET = iskating
TEMPLATE = app

# ---- Qt SDK selection ----
# RTSP playback uses FFmpeg/libav with D3D11VA hardware decoding.
# Use the official Qt MSVC 2022 x64 SDK installed by Qt Online Installer.
QT_ROOT = C:/Qt/6.7.3/msvc2022_64
QT_BIN_DIR = $$QT_ROOT/bin
QT_INCLUDE_DIR = $$QT_ROOT/include
QT_LIB_DIR = $$QT_ROOT/lib

!exists($$QT_BIN_DIR/qmake.exe) {
    error("Official Qt 6.7.3 MSVC 2022 x64 qmake.exe not found: $$QT_BIN_DIR/qmake.exe")
}

CURRENT_QT_PREFIX = $$[QT_INSTALL_PREFIX]
!equals(CURRENT_QT_PREFIX, $$QT_ROOT) {
    error("Use the official qmake at $$QT_BIN_DIR/qmake.exe; current qmake prefix=$$CURRENT_QT_PREFIX")
}

QMAKE_MOC = $$QT_BIN_DIR/moc.exe
QMAKE_UIC = $$QT_BIN_DIR/uic.exe
QMAKE_RCC = $$QT_BIN_DIR/rcc.exe
QMAKE_INCDIR_QT = $$QT_INCLUDE_DIR
QMAKE_LIBDIR_QT = $$QT_LIB_DIR
QMAKE_LIBDIR += $$QT_LIB_DIR

message("Current qmake Qt prefix=$$[QT_INSTALL_PREFIX]")

include(build/qmake/common.pri)
include(build/qmake/dependencies.pri)

include(src/app/app.pri)
include(src/ui/ui.pri)
include(src/domain/domain.pri)
include(src/application/application.pri)
include(src/infrastructure/video/video.pri)
include(src/infrastructure/inference/inference.pri)
include(src/infrastructure/persistence/persistence.pri)
include(src/infrastructure/configuration/configuration.pri)
include(resources/resources.pri)

message("Using Qt SDK=$$QT_ROOT")
include(build/qmake/deployment.pri)
