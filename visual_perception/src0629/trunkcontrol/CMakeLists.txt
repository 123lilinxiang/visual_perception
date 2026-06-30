cmake_minimum_required(VERSION 3.12)

project(trunk_head_control)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

# ==============================
# ROKAE SDK 路径
# ==============================

set(ROKAE_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(ROKAE_INCLUDE_DIR
    ${ROKAE_SDK_ROOT}/include
)

set(ROKAE_LIB_DIR
    ${ROKAE_SDK_ROOT}/lib/Linux/aarch64
)

# 你根据实际存在的库名选一个
# 优先用 .so
set(ROKAE_CORE_LIB
    ${ROKAE_LIB_DIR}/libxCoreSDK.so.0.6.1
)

# 如果你的目录里没有 libxCoreSDK.so，只有 libxCoreSDK.so.0.5.1，
# 就把上面改成：
# set(ROKAE_CORE_LIB
#     ${ROKAE_LIB_DIR}/libxCoreSDK.so.0.5.1
# )

# ==============================
# 可执行节点
# ==============================

add_executable(trunk_head_movej_node
    src/trunk_head_control.cpp
)

target_include_directories(trunk_head_movej_node PRIVATE
    ${ROKAE_INCLUDE_DIR}
    /usr/include/eigen3
)

ament_target_dependencies(trunk_head_movej_node
    rclcpp
    std_msgs
)

target_link_libraries(trunk_head_movej_node
    ${ROKAE_CORE_LIB}
    pthread
)

install(TARGETS
    trunk_head_movej_node
    DESTINATION lib/${PROJECT_NAME}
)

ament_package()