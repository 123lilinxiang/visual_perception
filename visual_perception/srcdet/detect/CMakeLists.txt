cmake_minimum_required(VERSION 3.18)

# ============================================================
# CUDA 编译器和架构
# 必须放在 project() 之前
# ============================================================
set(CMAKE_CUDA_COMPILER "/usr/local/cuda/bin/nvcc")

# Jetson Orin：Compute Capability 8.7
set(CMAKE_CUDA_ARCHITECTURES 87)

project(yolov8_detect LANGUAGES CXX CUDA)

# ============================================================
# 基础编译配置
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CUDA_FLAGS_RELEASE "-O3 -DNDEBUG")

# 使用动态 CUDA Runtime，适合 Jetson 系统环境
set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)

# 生成位置无关代码
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================================
# 自定义 CMake 模块
# ============================================================
list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Function.cmake")
    include(Function)
endif()

# ============================================================
# ROS2
# ============================================================
find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)

find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(message_filters REQUIRED)

# 自定义消息包
find_package(msg_det REQUIRED)

# ============================================================
# Eigen / YAML
# ============================================================
find_package(Eigen3 REQUIRED)
find_package(yaml-cpp REQUIRED)

# ============================================================
# CUDA
# ============================================================
find_package(CUDAToolkit REQUIRED)

message(STATUS "========================================")
message(STATUS "CUDA Toolkit version : ${CUDAToolkit_VERSION}")
message(STATUS "CUDA include dirs    : ${CUDAToolkit_INCLUDE_DIRS}")
message(STATUS "CUDA nvcc            : ${CMAKE_CUDA_COMPILER}")
message(STATUS "CUDA architectures   : ${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "========================================")

# ============================================================
# OpenCV
# ============================================================
find_package(OpenCV REQUIRED)

message(STATUS "OpenCV version       : ${OpenCV_VERSION}")
message(STATUS "OpenCV include dirs  : ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV libraries     : ${OpenCV_LIBRARIES}")

# ============================================================
# TensorRT
#
# Jetson Orin / JetPack 默认位置：
#
# 头文件：
#   /usr/include/aarch64-linux-gnu
#   /usr/include
#
# 动态库：
#   /usr/lib/aarch64-linux-gnu
# ============================================================

set(TENSORRT_INCLUDE_SEARCH_PATHS
    /usr/include/aarch64-linux-gnu
    /usr/include
    /usr/local/include
    /usr/src/tensorrt/include
)

set(TENSORRT_LIBRARY_SEARCH_PATHS
    /usr/lib/aarch64-linux-gnu
    /usr/lib
    /usr/local/lib
    /usr/local/cuda/lib64
)

# TensorRT 头文件
find_path(TensorRT_INCLUDE_DIR
    NAMES
        NvInfer.h
    PATHS
        ${TENSORRT_INCLUDE_SEARCH_PATHS}
)

# TensorRT 核心库
find_library(TensorRT_NVINFER_LIBRARY
    NAMES
        nvinfer
    PATHS
        ${TENSORRT_LIBRARY_SEARCH_PATHS}
)

# TensorRT 插件库
find_library(TensorRT_PLUGIN_LIBRARY
    NAMES
        nvinfer_plugin
    PATHS
        ${TENSORRT_LIBRARY_SEARCH_PATHS}
)

# TensorRT ONNX 解析库
find_library(TensorRT_ONNX_PARSER_LIBRARY
    NAMES
        nvonnxparser
    PATHS
        ${TENSORRT_LIBRARY_SEARCH_PATHS}
)

# ============================================================
# TensorRT 检查
# ============================================================
if(NOT TensorRT_INCLUDE_DIR)
    message(FATAL_ERROR
        "\n没有找到 TensorRT 头文件 NvInfer.h。\n"
        "请执行：\n"
        "  find /usr -name NvInfer.h 2>/dev/null\n"
        "或者安装：\n"
        "  sudo apt install libnvinfer-dev\n"
    )
endif()

if(NOT TensorRT_NVINFER_LIBRARY)
    message(FATAL_ERROR
        "\n没有找到 libnvinfer.so。\n"
        "请执行：\n"
        "  find /usr -name 'libnvinfer.so*' 2>/dev/null\n"
    )
endif()

if(NOT TensorRT_PLUGIN_LIBRARY)
    message(FATAL_ERROR
        "\n没有找到 libnvinfer_plugin.so。\n"
        "请执行：\n"
        "  find /usr -name 'libnvinfer_plugin.so*' 2>/dev/null\n"
    )
endif()

if(NOT TensorRT_ONNX_PARSER_LIBRARY)
    message(FATAL_ERROR
        "\n没有找到 libnvonnxparser.so。\n"
        "请执行：\n"
        "  find /usr -name 'libnvonnxparser.so*' 2>/dev/null\n"
    )
endif()

set(TensorRT_LIBRARIES
    ${TensorRT_NVINFER_LIBRARY}
    ${TensorRT_PLUGIN_LIBRARY}
    ${TensorRT_ONNX_PARSER_LIBRARY}
)

message(STATUS "========================================")
message(STATUS "TensorRT include     : ${TensorRT_INCLUDE_DIR}")
message(STATUS "TensorRT nvinfer     : ${TensorRT_NVINFER_LIBRARY}")
message(STATUS "TensorRT plugin      : ${TensorRT_PLUGIN_LIBRARY}")
message(STATUS "TensorRT ONNX parser : ${TensorRT_ONNX_PARSER_LIBRARY}")
message(STATUS "========================================")

# ============================================================
# 可执行程序
# ============================================================
add_executable(${PROJECT_NAME}
    main.cpp

    src/yolov8-detect.cpp
    src/cam2base.cpp

    include/yolov8-detect.h
    include/common.hpp
    include/filesystem.hpp
    include/cam2base.h
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        TRT_10
)

# ============================================================
# 头文件目录
# ============================================================
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
        ${TensorRT_INCLUDE_DIR}
        ${CUDAToolkit_INCLUDE_DIRS}
)

# ============================================================
# ROS2 依赖
#
# 注意：
# ament_target_dependencies 在 ROS2 Humble 内部可能使用
# plain target_link_libraries 写法。
#
# 因此，后面的 target_link_libraries 也必须使用 plain 写法，
# 不能写 PRIVATE/PUBLIC。
# ============================================================
ament_target_dependencies(${PROJECT_NAME}
    ament_index_cpp
    rclcpp
    sensor_msgs
    cv_bridge
    message_filters
    msg_det
)

# ============================================================
# 非 ROS2 库
#
# 这里必须使用 plain 签名。
# 不要添加 PRIVATE、PUBLIC、INTERFACE。
# ============================================================
target_link_libraries(${PROJECT_NAME}
    ${OpenCV_LIBRARIES}

    ${TensorRT_NVINFER_LIBRARY}
    ${TensorRT_PLUGIN_LIBRARY}
    ${TensorRT_ONNX_PARSER_LIBRARY}

    CUDA::cudart

    yaml-cpp
    Eigen3::Eigen

    pthread
    dl
)

# ============================================================
# CUDA 编译属性
# ============================================================
set_target_properties(${PROJECT_NAME}
    PROPERTIES
        CUDA_ARCHITECTURES 87
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_RESOLVE_DEVICE_SYMBOLS ON
)

# ============================================================
# RPATH
#
# 保证编译后和安装后的程序能找到 TensorRT/CUDA 动态库
# ============================================================
set_target_properties(${PROJECT_NAME}
    PROPERTIES
        BUILD_RPATH
            "/usr/lib/aarch64-linux-gnu;/usr/local/cuda/lib64"

        INSTALL_RPATH
            "/usr/lib/aarch64-linux-gnu;/usr/local/cuda/lib64"

        INSTALL_RPATH_USE_LINK_PATH TRUE
)

# ============================================================
# 安装可执行程序
# ============================================================
install(
    TARGETS ${PROJECT_NAME}
    DESTINATION lib/${PROJECT_NAME}
)

# ============================================================
# 安装配置文件
# ============================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config")
    install(
        DIRECTORY config/
        DESTINATION share/${PROJECT_NAME}/config
    )
endif()

# ============================================================
# 安装 launch 文件
# ============================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/launch")
    install(
        DIRECTORY launch/
        DESTINATION share/${PROJECT_NAME}/launch
    )
endif()

# ============================================================
# ROS2 包结束
# ============================================================
ament_package()