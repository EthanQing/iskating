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

include(common.pri)

QT += core gui widgets svg network printsupport

include(third_party/QXlsx/QXlsx/QXlsx.pri)

FFMPEG_ROOT = $$(FFMPEG_ROOT)
isEmpty(FFMPEG_ROOT) {
    FFMPEG_ROOT = C:/Users/qc/zm/ffmpeg-8.0.1-full_build-shared
}
FFMPEG_INCLUDE_DIR = $$FFMPEG_ROOT/include
FFMPEG_LIB_DIR = $$FFMPEG_ROOT/lib
FFMPEG_BIN_DIR = $$FFMPEG_ROOT/bin

!exists($$FFMPEG_INCLUDE_DIR/libavcodec/avcodec.h) {
    error("FFmpeg dev headers not found. Set FFMPEG_ROOT to a shared MSVC x64 FFmpeg dev package: $$FFMPEG_ROOT")
}
!exists($$FFMPEG_LIB_DIR/avcodec.lib) {
    error("FFmpeg import libs not found. Set FFMPEG_ROOT to a shared MSVC x64 FFmpeg dev package: $$FFMPEG_ROOT")
}

INCLUDEPATH += $$FFMPEG_INCLUDE_DIR
TENSORRT_ROOT = $$(TENSORRT_ROOT)
isEmpty(TENSORRT_ROOT) {
    TENSORRT_ROOT = C:/Program Files/TensorRT-10.1.0.27
}
CUDA_ROOT = $$(CUDA_ROOT)
isEmpty(CUDA_ROOT) {
    CUDA_ROOT = C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8
}
TENSORRT_INCLUDE_DIR = $$TENSORRT_ROOT/include
TENSORRT_LIB_DIR = $$TENSORRT_ROOT/lib
CUDA_INCLUDE_DIR = $$CUDA_ROOT/include
CUDA_LIB_DIR = $$CUDA_ROOT/lib/x64
CUDA_BIN_DIR = $$CUDA_ROOT/bin

!exists($$TENSORRT_INCLUDE_DIR/NvInfer.h) {
    error("TensorRT headers not found. Set TENSORRT_ROOT: $$TENSORRT_ROOT")
}
!exists($$TENSORRT_LIB_DIR/nvinfer_10.lib) {
    error("TensorRT import libs not found. Set TENSORRT_ROOT: $$TENSORRT_ROOT")
}
!exists($$CUDA_INCLUDE_DIR/cuda_runtime_api.h) {
    error("CUDA headers not found. Set CUDA_ROOT: $$CUDA_ROOT")
}
!exists($$CUDA_LIB_DIR/cudart.lib) {
    error("CUDA import lib not found. Set CUDA_ROOT: $$CUDA_ROOT")
}

INCLUDEPATH += "$$TENSORRT_INCLUDE_DIR" "$$CUDA_INCLUDE_DIR"
QMAKE_LIBDIR += $$FFMPEG_LIB_DIR "$$TENSORRT_LIB_DIR" "$$CUDA_LIB_DIR"
LIBS += \
    avformat.lib \
    avcodec.lib \
    avutil.lib \
    nvinfer_10.lib \
    nvonnxparser_10.lib \
    nvinfer_plugin_10.lib \
    cudart.lib \
    d3d11.lib \
    dxgi.lib \
    dxguid.lib \
    d3dcompiler.lib \
    user32.lib

message("Using FFmpeg SDK=$$FFMPEG_ROOT")
message("Using TensorRT SDK=$$TENSORRT_ROOT")
message("Using CUDA SDK=$$CUDA_ROOT")

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
    actionstandardscorer.cpp \
    cameraconfigtemplate.cpp \
    cameraconnectivitytester.cpp \
    d3d11videodevice.cpp \
    d3dframeextractor.cpp \
    d3dvideosurface.cpp \
    framelessdialog.cpp \
    handanalysismanager.cpp \
    iconutils.cpp \
    main.cpp \
    mainwindow.cpp \
    nvrplayback.cpp \
    offlinevideoprobe.cpp \
    personmanagementdialog.cpp \
    poseidentityresolver.cpp \
    poseresult.cpp \
    posestandardnessscorer.cpp \
    rtspstream.cpp \
    skeletonviewwidget.cpp \
    streamregistry.cpp \
    systemsettingsdialog.cpp \
    tensorrtbodyposebackend.cpp \
    tensorrtrtmw3dbackend.cpp \
    tensorrtrunner.cpp \
    trajectorywidget.cpp \
    trainingreviewdialog.cpp \
    trainingrepository.cpp \
    videostorageplan.cpp \
    videoopenglwidget.cpp

HEADERS += \
    actionstandardscorer.h \
    cameraconfigtemplate.h \
    cameraconnectivitytester.h \
    d3d11videodevice.h \
    d3dframe.h \
    d3dframeextractor.h \
    d3dvideosurface.h \
    framelessdialog.h \
    handanalysismanager.h \
    iconutils.h \
    mainwindow.h \
    nvrplayback.h \
    offlinevideoprobe.h \
    personmanagementdialog.h \
    poseidentityresolver.h \
    poseresult.h \
    posestandardnessscorer.h \
    rtspstream.h \
    skeletonviewwidget.h \
    streamregistry.h \
    systemsettingsdialog.h \
    tensorrtbodyposebackend.h \
    tensorrtrtmw3dbackend.h \
    tensorrtrunner.h \
    trajectorywidget.h \
    trainingdomain.h \
    trainingreviewdialog.h \
    trainingrepository.h \
    videostorageplan.h \
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

ffmpeg_dlls.commands = \
    $(COPY_FILE) $$shell_path($$FFMPEG_BIN_DIR/avcodec-62.dll) $$shell_path($$DESTDIR) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_path($$FFMPEG_BIN_DIR/avformat-62.dll) $$shell_path($$DESTDIR) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_path($$FFMPEG_BIN_DIR/avutil-60.dll) $$shell_path($$DESTDIR) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_path($$FFMPEG_BIN_DIR/swresample-6.dll) $$shell_path($$DESTDIR) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_path($$FFMPEG_BIN_DIR/swscale-9.dll) $$shell_path($$DESTDIR)
QMAKE_EXTRA_TARGETS += ffmpeg_dlls
POST_TARGETDEPS += ffmpeg_dlls

tensorrt_dlls.commands = \
    $(COPY_FILE) $$shell_quote($$shell_path($$TENSORRT_LIB_DIR/nvinfer_10.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$TENSORRT_LIB_DIR/nvinfer_plugin_10.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$TENSORRT_LIB_DIR/nvinfer_vc_plugin_10.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$TENSORRT_LIB_DIR/nvinfer_dispatch_10.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$TENSORRT_LIB_DIR/nvinfer_lean_10.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$TENSORRT_LIB_DIR/nvonnxparser_10.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$TENSORRT_LIB_DIR/nvinfer_builder_resource_10.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/cudart64_110.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/cublas64_11.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/cublasLt64_11.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/cufft64_10.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/cufftw64_10.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/curand64_10.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/cusolver64_11.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/cusolverMg64_11.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/cusparse64_11.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/nvrtc64_112_0.dll)) $$shell_quote($$shell_path($$DESTDIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$CUDA_BIN_DIR/nvrtc-builtins64_118.dll)) $$shell_quote($$shell_path($$DESTDIR))
QMAKE_EXTRA_TARGETS += tensorrt_dlls
POST_TARGETDEPS += tensorrt_dlls

ai_models.commands = \
    $(COPY_DIR) $$shell_quote($$shell_path($$PWD/models)) $$shell_quote($$shell_path($$DESTDIR/models))
QMAKE_EXTRA_TARGETS += ai_models
POST_TARGETDEPS += ai_models

CONFIG(debug, debug|release) {
    WINDOWS_PLATFORM_DLL = $$QT_ROOT/plugins/platforms/qwindowsd.dll
} else {
    WINDOWS_PLATFORM_DLL = $$QT_ROOT/plugins/platforms/qwindows.dll
}
WINDOWS_PLATFORM_DIR = $$DESTDIR/plugins/platforms

windows_platform_plugin.commands = \
    if not exist $$shell_quote($$shell_path($$WINDOWS_PLATFORM_DIR)) $(MKDIR) $$shell_quote($$shell_path($$WINDOWS_PLATFORM_DIR)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$WINDOWS_PLATFORM_DLL)) $$shell_quote($$shell_path($$WINDOWS_PLATFORM_DIR))
QMAKE_EXTRA_TARGETS += windows_platform_plugin
POST_TARGETDEPS += windows_platform_plugin

CONFIG(release, debug|release) {
    QMAKE_POST_LINK += $$escape_expand(\\n\\t) $$shell_quote($$shell_path($$QT_BIN_DIR/windeployqt.exe)) --release --no-translations $$shell_quote($$shell_path($$DESTDIR/$${TARGET}.exe))
}

# ---- MSVC: generate debug info in Release ----
PDB_PATH = $$shell_path($$DESTDIR/$${TARGET}.pdb)

msvc {
    QMAKE_CFLAGS_RELEASE += /Zi
    QMAKE_CXXFLAGS_RELEASE += /Zi
    QMAKE_LFLAGS_RELEASE += /DEBUG
    QMAKE_LFLAGS_RELEASE += /PDB:$$PDB_PATH
}
