cmake_minimum_required(VERSION 3.8)

set(CMAKE_CUDA_COMPILER /usr/local/cuda/bin/nvcc)
set(CMAKE_CUDA_ARCHITECTURES 60 61 62 70 72 75 86 89 90)

project(yolov8_obb LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

option(CUDA_USE_STATIC_CUDA_RUNTIME OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Function)

# =========================
# ROS2
# =========================
find_package(Eigen3 REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(message_filters REQUIRED)
find_package(msg_det REQUIRED)
# =========================
# CUDA
# =========================
find_package(CUDA REQUIRED)
print_var(CUDA_LIBRARIES)
print_var(CUDA_INCLUDE_DIRS)

get_filename_component(CUDA_LIB_DIR ${CUDA_LIBRARIES} DIRECTORY)
print_var(CUDA_LIB_DIR)

# =========================
# OpenCV
# =========================
find_package(OpenCV REQUIRED)
print_var(OpenCV_LIBS)
print_var(OpenCV_LIBRARIES)
print_var(OpenCV_INCLUDE_DIRS)

# =========================
# TensorRT 手动指定
# =========================
set(TENSORRT_ROOT /usr/local/TensorRT-8.6.1.6)
set(TensorRT_INCLUDE_DIRS ${TENSORRT_ROOT}/include)
set(TensorRT_LIB_DIR ${TENSORRT_ROOT}/lib)

set(TensorRT_LIBRARIES
    ${TENSORRT_ROOT}/lib/libnvinfer.so
    ${TENSORRT_ROOT}/lib/libnvonnxparser.so
    ${TENSORRT_ROOT}/lib/libnvinfer_plugin.so
)

print_var(TensorRT_LIBRARIES)
print_var(TensorRT_INCLUDE_DIRS)
print_var(TensorRT_LIB_DIR)

# =========================
# Include / Libs
# =========================
list(APPEND ALL_INCLUDE_DIRS
    ${CUDA_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${TensorRT_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

list(APPEND ALL_LIBS
    ${CUDA_LIBRARIES}
    ${OpenCV_LIBRARIES}
    ${TensorRT_LIBRARIES}
)

list(APPEND ALL_LIB_DIRS
    ${CUDA_LIB_DIR}
    ${TensorRT_LIB_DIR}
)

print_var(ALL_INCLUDE_DIRS)
print_var(ALL_LIBS)
print_var(ALL_LIB_DIRS)

# =========================
# Executable
# =========================
add_executable(${PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cam2robot.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/yolov8-obb.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/filesystem.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cam2robot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/yolov8-obb.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
    ${ALL_INCLUDE_DIRS}
)

target_link_directories(${PROJECT_NAME}
    PUBLIC
    ${ALL_LIB_DIRS}
)


# ROS2 dependencies
ament_target_dependencies(${PROJECT_NAME}
    rclcpp
    sensor_msgs
    cv_bridge
    message_filters
    ament_index_cpp
    msg_det
)

target_link_libraries(${PROJECT_NAME}
    ${ALL_LIBS}
    yaml-cpp
    Eigen3::Eigen
)

# =========================
# Install
# =========================
install(TARGETS ${PROJECT_NAME}
    DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY config
    DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY launch
    DESTINATION share/${PROJECT_NAME}
)
ament_package()