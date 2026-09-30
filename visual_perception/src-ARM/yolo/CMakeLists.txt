cmake_minimum_required(VERSION 3.18)
project(yolo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# This package has no .cu kernels. C++ calls the native CUDA/TensorRT runtime;
# do not force desktop GPU architectures or an x86 TensorRT SDK directory.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(message_filters REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(msg_det REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(OpenCV REQUIRED)
find_package(TensorRT 8 REQUIRED)

message(STATUS "Target CPU: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CUDA toolkit: ${CUDAToolkit_VERSION}")
message(STATUS "TensorRT: ${TensorRT_VERSION_STRING}")
message(STATUS "TensorRT includes: ${TensorRT_INCLUDE_DIRS}")
message(STATUS "TensorRT libraries: ${TensorRT_LIBRARIES}")
message(STATUS "OpenCV: ${OpenCV_VERSION} (${OpenCV_DIR})")

add_executable(${PROJECT_NAME}
    main.cpp
    src/cam2robot.cpp
    src/yolov8-obb.cpp
    src/yolov8-detection.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    ${OpenCV_INCLUDE_DIRS}
)
ament_target_dependencies(${PROJECT_NAME}
    rclcpp sensor_msgs cv_bridge message_filters ament_index_cpp msg_det
)
target_link_libraries(${PROJECT_NAME}
    CUDA::cudart TensorRT::TensorRT ${OpenCV_LIBRARIES}
    yaml-cpp Eigen3::Eigen
)

option(YOLO_BUILD_SMOKE_TEST "Build the camera-free TensorRT test" OFF)
if(YOLO_BUILD_SMOKE_TEST)
    add_executable(yolo_trt_smoke tests/trt_smoke.cpp
        src/yolov8-obb.cpp src/yolov8-detection.cpp)
    target_include_directories(yolo_trt_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include" ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(yolo_trt_smoke
        CUDA::cudart TensorRT::TensorRT ${OpenCV_LIBRARIES})
    install(TARGETS yolo_trt_smoke DESTINATION lib/${PROJECT_NAME})
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})
ament_package()
