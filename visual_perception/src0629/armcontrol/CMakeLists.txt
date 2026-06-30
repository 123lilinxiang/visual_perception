cmake_minimum_required(VERSION 3.8)
project(rokae_ros_control)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(msg_det REQUIRED)

# 当前 CMakeLists.txt 所在目录
# 也就是 /home/admin/vsproject/xCoreSDK-v0.7.1.ar_5
set(ROKAE_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# 你现在用的是 Orin / ARM64，所以是 aarch64
set(ROKAE_ARCH aarch64)

set(ROKAE_LIB_DIR ${ROKAE_SDK_ROOT}/lib/Linux/${ROKAE_ARCH})
set(ROKAE_G3LOG_LIB_DIR ${ROKAE_SDK_ROOT}/external/g3log/lib/${ROKAE_ARCH})
set(ROKAE_KDL_LIB_DIR ${ROKAE_SDK_ROOT}/external/kdl/lib/${ROKAE_ARCH})

message(STATUS "ROKAE_SDK_ROOT = ${ROKAE_SDK_ROOT}")
message(STATUS "ROKAE_LIB_DIR = ${ROKAE_LIB_DIR}")

# 检查库是否存在
if(NOT EXISTS "${ROKAE_LIB_DIR}/libxCoreSDK.so.0.7.1")
  message(FATAL_ERROR "Cannot find libxCoreSDK.so.0.7.1 at ${ROKAE_LIB_DIR}")
endif()

if(NOT EXISTS "${ROKAE_LIB_DIR}/libxMateModel.a")
  message(FATAL_ERROR "Cannot find libxMateModel.a at ${ROKAE_LIB_DIR}")
endif()

if(NOT EXISTS "${ROKAE_G3LOG_LIB_DIR}/libg3log.a")
  message(FATAL_ERROR "Cannot find libg3log.a at ${ROKAE_G3LOG_LIB_DIR}")
endif()

if(NOT EXISTS "${ROKAE_KDL_LIB_DIR}/liborocos-kdl.a")
  message(FATAL_ERROR "Cannot find liborocos-kdl.a at ${ROKAE_KDL_LIB_DIR}")
endif()

include_directories(
  ${ROKAE_SDK_ROOT}/include
  ${ROKAE_SDK_ROOT}/example
  ${ROKAE_SDK_ROOT}/external/g3log/include
  ${ROKAE_SDK_ROOT}/external/kdl/include
  /usr/include/eigen3
)

add_executable(rokae_movel_ros_node
  src/example/movel_control.cpp
)

ament_target_dependencies(rokae_movel_ros_node
  rclcpp
  std_msgs
  msg_det
)

target_link_libraries(rokae_movel_ros_node
  ${ROKAE_LIB_DIR}/libxCoreSDK.so.0.7.1
  ${ROKAE_LIB_DIR}/libxMateModel.a
  ${ROKAE_G3LOG_LIB_DIR}/libg3log.a
  ${ROKAE_KDL_LIB_DIR}/liborocos-kdl.a
  pthread
  dl
)

install(TARGETS
  rokae_movel_ros_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()