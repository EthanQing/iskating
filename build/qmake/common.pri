PROJECT_ROOT = $$clean_path($$PWD/../..)
SRC_ROOT = $$PROJECT_ROOT/src
RESOURCE_ROOT = $$PROJECT_ROOT/resources
BUILD_ROOT = $$PROJECT_ROOT/x64

CONFIG += c++20
DEFINES += NOMINMAX

INCLUDEPATH += \
    $$SRC_ROOT/app \
    $$SRC_ROOT/ui \
    $$SRC_ROOT/domain \
    $$SRC_ROOT/application \
    $$SRC_ROOT/infrastructure/video \
    $$SRC_ROOT/infrastructure/inference \
    $$SRC_ROOT/infrastructure/persistence \
    $$SRC_ROOT/infrastructure/configuration

win32:msvc {
    QMAKE_TARGET.arch = x86_64
    QMAKE_LFLAGS += /MACHINE:X64
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8 /MP

    QMAKE_CFLAGS_RELEASE -= -MT
    QMAKE_CFLAGS_DEBUG -= -MT
    QMAKE_CXXFLAGS_RELEASE -= -MT
    QMAKE_CXXFLAGS_DEBUG -= -MT
    QMAKE_CFLAGS_RELEASE += /MD
    QMAKE_CFLAGS_DEBUG += /MDd
    QMAKE_CXXFLAGS_RELEASE += /MD
    QMAKE_CXXFLAGS_DEBUG += /MDd
}

message("PROJECT_ROOT=$$PROJECT_ROOT")
