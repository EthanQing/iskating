#-------------------------------------------------
# iSkating Coach Qt Widgets interface
# Converted from vue2-empty/src/App.vue
#-------------------------------------------------

TARGET = iskating
TEMPLATE = app

include(common.pri)

QT += core gui widgets opengl openglwidgets svg

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20
win32:CONFIG += debug_and_release

DEFINES += NOMINMAX

win32:msvc {
    # Generate the Visual Studio project as x64 by default.
    QMAKE_TARGET.arch = x86_64
    QMAKE_LFLAGS += /MACHINE:X64
}

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    videoopenglwidget.cpp

HEADERS += \
    mainwindow.h \
    videoopenglwidget.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    iskating.qrc

INCLUDEPATH += .

# common.pri defines the local Qt/OSGeo4W root used by this project.
isEmpty(OSGEO4W_DIR) {
    OSGEO4W_DIR = c:/osgeo4w
}
message("Using OSGEO4W_DIR=$$OSGEO4W_DIR")

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
