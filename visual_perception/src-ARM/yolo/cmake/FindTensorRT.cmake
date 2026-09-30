# Native JetPack installs use /usr/{include,lib}/aarch64-linux-gnu.
# Override with -DTensorRT_ROOT=/path/to/a/native/SDK when necessary.
set(TensorRT_ROOT "" CACHE PATH "Native TensorRT SDK root (optional)")
set(_trt_search_args)
if(TensorRT_ROOT)
    list(APPEND _trt_search_args PATHS "${TensorRT_ROOT}" NO_DEFAULT_PATH)
endif()
find_path(TensorRT_INCLUDE_DIR NvInfer.h ${_trt_search_args}
    PATH_SUFFIXES include "include/${CMAKE_LIBRARY_ARCHITECTURE}")
find_library(TensorRT_LIBRARY nvinfer ${_trt_search_args}
    PATH_SUFFIXES lib lib64 "lib/${CMAKE_LIBRARY_ARCHITECTURE}")
find_library(TensorRT_PLUGIN_LIBRARY nvinfer_plugin ${_trt_search_args}
    PATH_SUFFIXES lib lib64 "lib/${CMAKE_LIBRARY_ARCHITECTURE}")

if(TensorRT_INCLUDE_DIR AND EXISTS "${TensorRT_INCLUDE_DIR}/NvInferVersion.h")
    foreach(_part MAJOR MINOR PATCH)
        file(STRINGS "${TensorRT_INCLUDE_DIR}/NvInferVersion.h" _line
            REGEX "^#define[ \t]+NV_TENSORRT_${_part}[ \t]+[0-9]+")
        string(REGEX REPLACE ".*NV_TENSORRT_${_part}[ \t]+([0-9]+).*" "\\1"
            TensorRT_VERSION_${_part} "${_line}")
    endforeach()
    set(TensorRT_VERSION_STRING
        "${TensorRT_VERSION_MAJOR}.${TensorRT_VERSION_MINOR}.${TensorRT_VERSION_PATCH}")
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TensorRT
    REQUIRED_VARS TensorRT_INCLUDE_DIR TensorRT_LIBRARY TensorRT_PLUGIN_LIBRARY
    VERSION_VAR TensorRT_VERSION_STRING)
if(TensorRT_FOUND)
    set(TensorRT_INCLUDE_DIRS "${TensorRT_INCLUDE_DIR}")
    set(TensorRT_LIBRARIES "${TensorRT_LIBRARY}" "${TensorRT_PLUGIN_LIBRARY}")
    if(NOT TARGET TensorRT::TensorRT)
        add_library(TensorRT::TensorRT INTERFACE IMPORTED)
        set_target_properties(TensorRT::TensorRT PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${TensorRT_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${TensorRT_LIBRARIES}")
    endif()
endif()
mark_as_advanced(TensorRT_INCLUDE_DIR TensorRT_LIBRARY TensorRT_PLUGIN_LIBRARY)
