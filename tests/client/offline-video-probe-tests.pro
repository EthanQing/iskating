QT += core network testlib
TEMPLATE = app
DEFINES += NOMINMAX
CONFIG += console testcase c++20
CONFIG -= app_bundle
QMAKE_CXXFLAGS += /utf-8
PROJECT_ROOT = $$clean_path($$PWD/../..)
FFMPEG_ROOT = $$(FFMPEG_ROOT)
isEmpty(FFMPEG_ROOT): FFMPEG_ROOT = C:/Users/qc/zm/ffmpeg-8.0.1-full_build-shared
INCLUDEPATH += $$PROJECT_ROOT/src/application $$PROJECT_ROOT/src/domain \
    $$PROJECT_ROOT/src/infrastructure/video $$PROJECT_ROOT/src/infrastructure/persistence $$FFMPEG_ROOT/include
LIBS += -L$$quote($$FFMPEG_ROOT/lib) -lavformat -lavcodec -lavutil -ld3d11 -ldxgi
SOURCES += $$PWD/tst_offlinevideoprobe.cpp \
    $$PROJECT_ROOT/src/infrastructure/video/offlinevideoprobe.cpp \
    $$PROJECT_ROOT/src/application/analysistaskmanager.cpp \
    $$PROJECT_ROOT/src/infrastructure/persistence/trainingrepository.cpp
HEADERS += $$PROJECT_ROOT/src/application/analysistaskmanager.h
TARGET = offline-video-probe-tests
contains(CONFIG, forced_fallback) {
    DEFINES += FORCED_FALLBACK
    TARGET = offline-video-probe-fallback-tests
} else {
    SOURCES += $$PROJECT_ROOT/src/infrastructure/video/d3d11videodevice.cpp
}
DESTDIR = $$PROJECT_ROOT/x64/Release/tests
OBJECTS_DIR = $$DESTDIR/$$TARGET-obj
MOC_DIR = $$DESTDIR/$$TARGET-moc
