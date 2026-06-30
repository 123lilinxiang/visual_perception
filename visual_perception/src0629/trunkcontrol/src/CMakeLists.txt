#-------------------------------------------------------------------------------
# Application (trunk server)

project(xCoreSDK-app CXX)

find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)

add_compile_definitions(
    $<$<CXX_COMPILER_ID:MSVC>:_USE_MATH_DEFINES>
    $<$<BOOL:${XCORE_LINK_SHARED_LIBS}>:XCORESDK_DLL>)

add_executable(trunk_movej_demo
    trunk_movej_demo.cpp
)

add_executable(trunk_cartesian_demo
    trunk_cartesian_demo.cpp
)

target_include_directories(trunk_movej_demo PUBLIC
    ${SHARED_INCLUDEDIR}
    ${YAML_CPP_INCLUDE_DIRS})

target_include_directories(trunk_cartesian_demo PUBLIC
    ${SHARED_INCLUDEDIR}
    ${YAML_CPP_INCLUDE_DIRS})

if(TARGET yaml-cpp::yaml-cpp)
    target_link_libraries(trunk_movej_demo
        Rokae::Rokae
        Threads::Threads
        eigen
        kdl
        g3log
        yaml-cpp::yaml-cpp)
    target_link_libraries(trunk_cartesian_demo
        Rokae::Rokae
        Threads::Threads
        eigen
        kdl
        g3log
        yaml-cpp::yaml-cpp)
else()
    target_link_libraries(trunk_movej_demo
        Rokae::Rokae
        Threads::Threads
        eigen
        kdl
        g3log
        ${YAML_CPP_LIBRARIES})
    target_link_libraries(trunk_cartesian_demo
        Rokae::Rokae
        Threads::Threads
        eigen
        kdl
        g3log
        ${YAML_CPP_LIBRARIES})
endif()

install(TARGETS trunk_movej_demo
    trunk_cartesian_demo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    OPTIONAL)
