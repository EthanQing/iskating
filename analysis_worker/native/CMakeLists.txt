cmake_minimum_required(VERSION 3.20)
project(iskating_fullrate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(DEEPSTREAM_ROOT "/opt/nvidia/deepstream/deepstream" CACHE PATH "DeepStream installation")
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
find_package(ZLIB REQUIRED)

add_executable(iskating-fullrate fullrate_pipeline.cpp)
target_include_directories(iskating-fullrate PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
    "${DEEPSTREAM_ROOT}/sources/includes"
)
target_link_directories(iskating-fullrate PRIVATE ${GSTREAMER_LIBRARY_DIRS} "${DEEPSTREAM_ROOT}/lib")
target_link_libraries(iskating-fullrate PRIVATE ${GSTREAMER_LIBRARIES} nvds_meta ZLIB::ZLIB)
target_compile_options(iskating-fullrate PRIVATE ${GSTREAMER_CFLAGS_OTHER})
