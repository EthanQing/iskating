QT += core gui widgets svg network printsupport

QXLSX_PARENTPATH = $$PROJECT_ROOT/third_party/QXlsx/QXlsx/../
QXLSX_HEADERPATH = $$PROJECT_ROOT/third_party/QXlsx/QXlsx/header/
QXLSX_SOURCEPATH = $$PROJECT_ROOT/third_party/QXlsx/QXlsx/source/
include($$PROJECT_ROOT/third_party/QXlsx/QXlsx/QXlsx.pri)

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

CONFIG += debug_and_release

message("Using FFmpeg SDK=$$FFMPEG_ROOT")
message("Using TensorRT SDK=$$TENSORRT_ROOT")
message("Using CUDA SDK=$$CUDA_ROOT")
