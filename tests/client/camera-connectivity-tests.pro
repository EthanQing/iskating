QT += core widgets network testlib
TEMPLATE = app
TARGET = camera-connectivity-tests
CONFIG += console testcase c++20
CONFIG -= app_bundle
QMAKE_CXXFLAGS += /utf-8

PROJECT_ROOT = $$clean_path($$PWD/../..)
FFMPEG_ROOT = $$(FFMPEG_ROOT)
isEmpty(FFMPEG_ROOT): FFMPEG_ROOT = C:/Users/qc/zm/ffmpeg-8.0.1-full_build-shared
INCLUDEPATH += $$PROJECT_ROOT/src/ui $$PROJECT_ROOT/src/domain \
    $$PROJECT_ROOT/src/infrastructure/configuration $$FFMPEG_ROOT/include
LIBS += -L$$quote($$FFMPEG_ROOT/lib) -lavformat -lavcodec -lavutil
SOURCES += $$PWD/tst_cameraconnectivity.cpp \
    $$PROJECT_ROOT/src/infrastructure/configuration/cameraconnectivitytester.cpp
HEADERS += $$PROJECT_ROOT/src/infrastructure/configuration/cameraconnectivitytester.h
DESTDIR = $$PROJECT_ROOT/x64/Release/tests
OBJECTS_DIR = $$PROJECT_ROOT/x64/Release/tests/connectivity-obj
MOC_DIR = $$PROJECT_ROOT/x64/Release/tests/connectivity-moc
