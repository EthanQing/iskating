QT += core widgets testlib

TEMPLATE = app
TARGET = client-tests
CONFIG += testcase c++20

PROJECT_ROOT = $$clean_path($$PWD/../..)
SRC_ROOT = $$PROJECT_ROOT/src

INCLUDEPATH += \
    $$SRC_ROOT/ui \
    $$SRC_ROOT/domain \
    $$SRC_ROOT/infrastructure/inference \
    $$SRC_ROOT/infrastructure/configuration \
    $$SRC_ROOT/infrastructure/persistence

SOURCES += \
    $$PWD/tst_clientcontracts.cpp \
    $$SRC_ROOT/infrastructure/inference/athletedetectionroi.cpp \
    $$SRC_ROOT/infrastructure/inference/athletetracker.cpp \
    $$SRC_ROOT/infrastructure/configuration/cameraconfigtemplate.cpp \
    $$SRC_ROOT/infrastructure/persistence/videostorageplan.cpp

HEADERS += \
    $$SRC_ROOT/ui/framelessdialog.h \
    $$SRC_ROOT/ui/systemsettingsdialog.h \
    $$SRC_ROOT/domain/trainingdomain.h \
    $$SRC_ROOT/infrastructure/inference/athletedetectionroi.h \
    $$SRC_ROOT/infrastructure/inference/athletetracker.h \
    $$SRC_ROOT/infrastructure/configuration/cameraconfigtemplate.h \
    $$SRC_ROOT/infrastructure/persistence/videostorageplan.h

CONFIG(release, debug|release) {
    DESTDIR = $$PROJECT_ROOT/x64/Release/tests
    OBJECTS_DIR = $$PROJECT_ROOT/x64/Release/tests/obj
    MOC_DIR = $$PROJECT_ROOT/x64/Release/tests/moc
} else {
    DESTDIR = $$PROJECT_ROOT/x64/Debug/tests
    OBJECTS_DIR = $$PROJECT_ROOT/x64/Debug/tests/obj
    MOC_DIR = $$PROJECT_ROOT/x64/Debug/tests/moc
}
