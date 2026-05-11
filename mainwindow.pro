#-------------------------------------------------
# iSkating Coach Qt Widgets interface
# Converted from vue2-empty/src/App.vue
#-------------------------------------------------

TARGET = iskating
TEMPLATE = app

# ---- Qt SDK selection ----
# RTSP playback requires the Qt Multimedia module with the FFmpeg backend.
# The OSGeo4W Qt 6.8.1 build used before does not provide the required backend,
# so this project is pinned to the local Qt 6.9.3 MSVC 2022 x64 SDK.
QT_ROOT = C:/Qt6vs2022/6.9.3/msvc2022_64
QT_BIN_DIR = $$QT_ROOT/bin
QT_INCLUDE_DIR = $$QT_ROOT/include
QT_LIB_DIR = $$QT_ROOT/lib

!exists($$QT_BIN_DIR/qmake.exe) {
    error("Qt 6.9.3 qmake.exe not found: $$QT_BIN_DIR/qmake.exe")
}

QMAKE_MOC = $$QT_BIN_DIR/moc.exe
QMAKE_UIC = $$QT_BIN_DIR/uic.exe
QMAKE_RCC = $$QT_BIN_DIR/rcc.exe
QMAKE_INCDIR_QT = $$QT_INCLUDE_DIR
QMAKE_LIBDIR_QT = $$QT_LIB_DIR
QMAKE_LIBDIR += $$QT_LIB_DIR

message("Current qmake Qt prefix=$$[QT_INSTALL_PREFIX]")

include(common.pri)

QT += core gui widgets opengl openglwidgets svg multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20
win32:CONFIG += debug_and_release

DEFINES += NOMINMAX QT_DEBUG_PLUGINS

win32:msvc {
    # Generate the Visual Studio project as x64 by default.
    QMAKE_TARGET.arch = x86_64
    QMAKE_LFLAGS += /MACHINE:X64
}

SOURCES += \
    framelessdialog.cpp \
    iconutils.cpp \
    main.cpp \
    mainwindow.cpp \
    videoopenglwidget.cpp

HEADERS += \
    framelessdialog.h \
    iconutils.h \
    mainwindow.h \
    videoopenglwidget.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    iskating.qrc

INCLUDEPATH += .

message("Using Qt SDK=$$QT_ROOT")

msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
    QMAKE_CXXFLAGS += /MP

    QMAKE_CFLAGS_RELEASE -= -MT
    QMAKE_CFLAGS_DEBUG -= -MT
    QMAKE_CXXFLAGS_RELEASE -= -MT
    QMAKE_CXXFLAGS_DEBUG -= -MT
    QMAKE_CFLAGS_RELEASE += /MD
    QMAKE_CFLAGS_DEBUG += /MDd
    QMAKE_CXXFLAGS_RELEASE += /MD
    QMAKE_CXXFLAGS_DEBUG += /MDd
}

# ---- output location ----
# Put generated x64 executables under this project directory:
#   Release: C:/own/iskating/x64/Release/iskating.exe
#   Debug:   C:/own/iskating/x64/Debug/iskating.exe
BUILD_ROOT = $$absolute_path($$PWD/x64)
CONFIG(release, debug|release) {
    BUILD_TYPE = Release
    DESTDIR     = $$BUILD_ROOT/Release
    OBJECTS_DIR = $$BUILD_ROOT/Release/obj
    MOC_DIR     = $$BUILD_ROOT/Release/moc
    RCC_DIR     = $$BUILD_ROOT/Release/rcc
    UI_DIR      = $$BUILD_ROOT/Release/ui
} else:CONFIG(debug, debug|release) {
    BUILD_TYPE = Debug
    DESTDIR     = $$BUILD_ROOT/Debug
    OBJECTS_DIR = $$BUILD_ROOT/Debug/obj
    MOC_DIR     = $$BUILD_ROOT/Debug/moc
    RCC_DIR     = $$BUILD_ROOT/Debug/rcc
    UI_DIR      = $$BUILD_ROOT/Debug/ui
}

message("Build output: $$DESTDIR")

# ---- MSVC: generate debug info in Release ----
PDB_PATH = $$shell_path($$DESTDIR/$${TARGET}.pdb)

msvc {
    QMAKE_CFLAGS_RELEASE += /Zi
    QMAKE_CXXFLAGS_RELEASE += /Zi
    QMAKE_LFLAGS_RELEASE += /DEBUG
    QMAKE_LFLAGS_RELEASE += /PDB:$$PDB_PATH
}
