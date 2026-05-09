# generated from ament/cmake/core/templates/nameConfig.cmake.in

# prevent multiple inclusion
if(_yolov8_obb_CONFIG_INCLUDED)
  # ensure to keep the found flag the same
  if(NOT DEFINED yolov8_obb_FOUND)
    # explicitly set it to FALSE, otherwise CMake will set it to TRUE
    set(yolov8_obb_FOUND FALSE)
  elseif(NOT yolov8_obb_FOUND)
    # use separate condition to avoid uninitialized variable warning
    set(yolov8_obb_FOUND FALSE)
  endif()
  return()
endif()
set(_yolov8_obb_CONFIG_INCLUDED TRUE)

# output package information
if(NOT yolov8_obb_FIND_QUIETLY)
  message(STATUS "Found yolov8_obb: 0.0.1 (${yolov8_obb_DIR})")
endif()

# warn when using a deprecated package
if(NOT "" STREQUAL "")
  set(_msg "Package 'yolov8_obb' is deprecated")
  # append custom deprecation text if available
  if(NOT "" STREQUAL "TRUE")
    set(_msg "${_msg} ()")
  endif()
  # optionally quiet the deprecation message
  if(NOT ${yolov8_obb_DEPRECATED_QUIET})
    message(DEPRECATION "${_msg}")
  endif()
endif()

# flag package as ament-based to distinguish it after being find_package()-ed
set(yolov8_obb_FOUND_AMENT_PACKAGE TRUE)

# include all config extra files
set(_extras "")
foreach(_extra ${_extras})
  include("${yolov8_obb_DIR}/${_extra}")
endforeach()
