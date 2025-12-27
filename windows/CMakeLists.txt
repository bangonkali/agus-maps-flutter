# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "agus_maps_flutter")
project(${PROJECT_NAME} LANGUAGES CXX)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "agus_maps_flutter_plugin")

# Define the plugin library target
add_library(${PLUGIN_NAME} SHARED
  "agus_maps_flutter_plugin.cpp"
  "agus_maps_flutter_plugin.h"
  "agus_maps_flutter_plugin_c_api.cpp"
  "include/agus_maps_flutter/agus_maps_flutter_plugin_c_api.h"
)

# Apply standard settings for Windows plugins
apply_standard_settings(${PLUGIN_NAME})

# Symbols are exported by default on Windows
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET default
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# Plugin include directories
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Link Flutter library
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin)

# Link Windows system libraries
target_link_libraries(${PLUGIN_NAME} PRIVATE
  shell32
  ole32
)

# Invoke the build for native code shared with the other target platforms.
# This builds the agus_maps_flutter shared library with CoMaps integration.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../src" "${CMAKE_CURRENT_BINARY_DIR}/shared")

# Link the plugin to our native library
target_link_libraries(${PLUGIN_NAME} PRIVATE agus_maps_flutter)

# Get ANGLE library directory - check multiple locations
set(ANGLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/angle")
get_filename_component(ANGLE_DIR "${ANGLE_DIR}" ABSOLUTE)

# Also check vcpkg_installed directory as fallback
set(VCPKG_ANGLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg_installed/x64-windows/bin")
get_filename_component(VCPKG_ANGLE_DIR "${VCPKG_ANGLE_DIR}" ABSOLUTE)

# Use vcpkg ANGLE if build/angle doesn't have it
if(NOT EXISTS "${ANGLE_DIR}/libEGL.dll" AND EXISTS "${VCPKG_ANGLE_DIR}/libEGL.dll")
  message(STATUS "Using vcpkg ANGLE libraries from: ${VCPKG_ANGLE_DIR}")
  set(ANGLE_DIR "${VCPKG_ANGLE_DIR}")
endif()

# Build the list of libraries to bundle with the plugin
set(_bundled_libs
  # Our native library
  $<TARGET_FILE:agus_maps_flutter>
)

# If ANGLE libraries exist, add them to bundled libraries
if(EXISTS "${ANGLE_DIR}/libEGL.dll")
  message(STATUS "Found ANGLE libraries at: ${ANGLE_DIR}")
  list(APPEND _bundled_libs
    "${ANGLE_DIR}/libEGL.dll"
    "${ANGLE_DIR}/libGLESv2.dll"
  )
  if(EXISTS "${ANGLE_DIR}/d3dcompiler_47.dll")
    list(APPEND _bundled_libs
      "${ANGLE_DIR}/d3dcompiler_47.dll"
    )
  endif()
else()
  message(WARNING "ANGLE libraries not found at: ${ANGLE_DIR}")
  message(WARNING "Run ./scripts/bootstrap_windows.ps1 to download ANGLE, or manually copy libEGL.dll and libGLESv2.dll")
endif()

# Bundle zlib1.dll which is required by ANGLE (via vcpkg)
if(EXISTS "${VCPKG_ANGLE_DIR}/zlib1.dll")
  message(STATUS "Found zlib1.dll at: ${VCPKG_ANGLE_DIR}")
  list(APPEND _bundled_libs
    "${VCPKG_ANGLE_DIR}/zlib1.dll"
  )
endif()

# Export the bundled libraries list to parent scope
set(agus_maps_flutter_bundled_libraries ${_bundled_libs} PARENT_SCOPE)
