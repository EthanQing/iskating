cmake_minimum_required(VERSION 3.20)
project(iskating_yolo26x_parser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(DEEPSTREAM_ROOT "/opt/nvidia/deepstream/deepstream" CACHE PATH "DeepStream installation")

add_library(iskating_yolo26x_parser SHARED yolo26x_parser.cpp)
target_include_directories(iskating_yolo26x_parser PRIVATE "${DEEPSTREAM_ROOT}/sources/includes")
set_target_properties(iskating_yolo26x_parser PROPERTIES OUTPUT_NAME "iskating_yolo26x_parser")
