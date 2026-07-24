# Keep generated binaries and intermediate files out of the source tree.
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
    if not exist $$shell_quote($$shell_path($$DESTDIR/models/athlete)) $(MKDIR) $$shell_quote($$shell_path($$DESTDIR/models/athlete)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$PROJECT_ROOT/models/athlete/athlete_models.json)) $$shell_quote($$shell_path($$DESTDIR/models/athlete/athlete_models.json)) $$escape_expand(\\n\\t) \
    $(COPY_FILE) $$shell_quote($$shell_path($$PROJECT_ROOT/models/athlete/athlete_models.sha256)) $$shell_quote($$shell_path($$DESTDIR/models/athlete/athlete_models.sha256)) $$escape_expand(\\n\\t) \
    if exist $$shell_quote($$shell_path($$PROJECT_ROOT/models/athlete/yolo26x.onnx)) $(COPY_FILE) $$shell_quote($$shell_path($$PROJECT_ROOT/models/athlete/yolo26x.onnx)) $$shell_quote($$shell_path($$DESTDIR/models/athlete/yolo26x.onnx)) $$escape_expand(\\n\\t) \
    if exist $$shell_quote($$shell_path($$PROJECT_ROOT/models/athlete/personvit_msmt17_vit_base.onnx)) $(COPY_FILE) $$shell_quote($$shell_path($$PROJECT_ROOT/models/athlete/personvit_msmt17_vit_base.onnx)) $$shell_quote($$shell_path($$DESTDIR/models/athlete/personvit_msmt17_vit_base.onnx))
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

# Release builds keep PDB output beside the executable for field diagnostics.
PDB_PATH = $$shell_path($$DESTDIR/$${TARGET}.pdb)
msvc {
    QMAKE_CFLAGS_RELEASE += /Zi
    QMAKE_CXXFLAGS_RELEASE += /Zi
    QMAKE_LFLAGS_RELEASE += /DEBUG
    QMAKE_LFLAGS_RELEASE += /PDB:$$PDB_PATH
}
