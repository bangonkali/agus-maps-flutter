cmake_minimum_required(VERSION 3.22.1)

# Force CMake Re-run 2
project(agus_maps_flutter_library LANGUAGES C CXX)

# Match CoMaps C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Windows vcpkg Integration
# ============================================================================
if(WIN32)
  # Set CMAKE_PREFIX_PATH to find vcpkg-installed packages
  set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg_installed/x64-windows")
  get_filename_component(VCPKG_INSTALLED_DIR "${VCPKG_INSTALLED_DIR}" ABSOLUTE)
  if(EXISTS "${VCPKG_INSTALLED_DIR}")
    message(STATUS "Found vcpkg installed packages at: ${VCPKG_INSTALLED_DIR}")
    list(APPEND CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}")
    # Also set include and library paths for packages that don't have proper CMake configs
    include_directories("${VCPKG_INSTALLED_DIR}/include")
    link_directories("${VCPKG_INSTALLED_DIR}/lib")
  else()
    message(WARNING "vcpkg packages not found. Run: vcpkg install --triplet x64-windows")
  endif()

  # MSVC-specific flags for Windows builds
  # /bigobj: Required for large template-heavy libraries like opening_hours (Spirit X3 parsers)
  #          which exceed the default COFF section limit (error C1128)
  if(MSVC)
    add_compile_options(/bigobj)
    message(STATUS "Added /bigobj flag for MSVC to handle large object files")
    
    # Suppress MSVC deprecation and conversion warnings for third-party code
    # These are mostly harmless in comaps/protobuf but generate thousands of warnings
    add_compile_definitions(
      _CRT_SECURE_NO_WARNINGS          # Suppress unsafe CRT function warnings (ctime, strcpy, sprintf)
      _CRT_NONSTDC_NO_DEPRECATE        # Suppress POSIX function deprecation warnings
      _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING  # std::iterator deprecated in C++17
      _SILENCE_ALL_CXX20_DEPRECATION_WARNINGS                 # std::is_pod and others deprecated in C++20
      _SILENCE_ALL_CXX23_DEPRECATION_WARNINGS                 # Future-proof for C++23
    )
    
    # Suppress specific MSVC warnings that flood the build output:
    # /wd4244: 'conversion': conversion from 'type1' to 'type2', possible loss of data
    # /wd4267: 'var': conversion from 'size_t' to 'type', possible loss of data  
    # /wd4146: unary minus operator applied to unsigned type
    # /wd4005: macro redefinition (WIN32_LEAN_AND_MEAN etc.)
    add_compile_options(/wd4244 /wd4267 /wd4146 /wd4005)
    message(STATUS "Added MSVC warning suppressions for third-party code")
    
    # Fix CoMaps Debug/Release detection conflict on Windows
    # MSVC automatically defines _DEBUG in Debug builds. CoMaps' base.hpp uses
    # a static_assert to ensure only Debug OR Release is defined, not both.
    # We need to undefine NDEBUG in Debug builds to prevent the assertion failure.
    # Using generator expressions to handle multi-config builds properly.
    add_compile_definitions(
      $<$<CONFIG:Debug>:DEBUG>
      $<$<CONFIG:Debug>:NRELEASE>
      $<$<NOT:$<CONFIG:Debug>>:RELEASE>
      $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    )
    # Remove any default NDEBUG that might be added
    string(REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  endif()
endif()

# ============================================================================
# Build Mode Detection
# ============================================================================
# USE_PREBUILT_COMAPS: Set by Gradle for external consumers with pre-built libs
# PREBUILT_DIR: Path to pre-built libraries (e.g., android/prebuilt/<abi>)
# COMAPS_HEADERS_DIR: Path to downloaded headers (e.g., android/headers)
option(USE_PREBUILT_COMAPS "Use pre-built CoMaps libraries instead of building from source" OFF)
set(PREBUILT_DIR "" CACHE PATH "Path to pre-built CoMaps libraries")
set(COMAPS_HEADERS_DIR "" CACHE PATH "Path to downloaded CoMaps headers")

if(USE_PREBUILT_COMAPS)
  message(STATUS "=== Using pre-built CoMaps libraries ===")
  message(STATUS "PREBUILT_DIR: ${PREBUILT_DIR}")
  message(STATUS "COMAPS_HEADERS_DIR: ${COMAPS_HEADERS_DIR}")
else()
  message(STATUS "=== Building CoMaps from source ===")
endif()

# ============================================================================
# CoMaps Configuration
# ============================================================================
if(NOT USE_PREBUILT_COMAPS)
  # CoMaps source directory
  set(COMAPS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/comaps")
  get_filename_component(COMAPS_SOURCE_DIR "${COMAPS_SOURCE_DIR}" ABSOLUTE)
  
  # On Windows, resolve symlinks to avoid MAX_PATH issues with the very long
  # Flutter plugin symlink paths (e.g., .plugin_symlinks\agus_maps_flutter\...)
  if(WIN32)
    get_filename_component(COMAPS_SOURCE_DIR "${COMAPS_SOURCE_DIR}" REALPATH)
    message(STATUS "Resolved COMAPS_SOURCE_DIR (for MAX_PATH): ${COMAPS_SOURCE_DIR}")
  endif()
  
  # CoMaps Options - only needed when building from source
  set(SKIP_TESTS ON CACHE BOOL "" FORCE)
  set(SKIP_QT ON CACHE BOOL "" FORCE)
  set(SKIP_QT_GUI ON CACHE BOOL "" FORCE)
  set(WITH_SYSTEM_PROVIDED_3PARTY OFF CACHE BOOL "" FORCE)
  set(SKIP_ANDROID_JNI ON CACHE BOOL "" FORCE)
  set(SKIP_PROTOBUF_CHECK ON CACHE BOOL "" FORCE)
  set(SKIP_TOOLS ON CACHE BOOL "" FORCE)

  # ============================================================================
  # Thread Checker Disable for Embedded Builds (Windows Only)
  # ============================================================================
  # In embedded/plugin scenarios (like Flutter), the threading model is different
  # from standalone CoMaps app. Framework objects may be created on one thread 
  # but accessed from Flutter's platform thread. Disable strict thread checking
  # to allow this usage pattern. This must be set BEFORE add_subdirectory so it
  # applies to all CoMaps libraries (map, drape_frontend, etc.)
  if(WIN32)
    add_compile_definitions(OMIM_DISABLE_THREAD_CHECKER)
    message(STATUS "Thread checker disabled for embedded Windows build")
  endif()

  # Platform detection is handled by OmimPlatform.cmake based on CMAKE_SYSTEM_NAME
  # - For Android: SKIP_ANDROID_JNI tells CoMaps to use our external platform impl
  # - For other platforms: SKIP_QT tells CoMaps to use dummy implementations
  # Note: OMIM_ROOT is now correctly set inside CoMaps CMakeLists.txt using
  # CMAKE_CURRENT_SOURCE_DIR, so it works when added as subdirectory

  # Build CoMaps
  add_subdirectory("${COMAPS_SOURCE_DIR}" build/comaps EXCLUDE_FROM_ALL)
endif()

# ============================================================================
# Agus Maps Flutter Library
# ============================================================================
# Platform-specific source files
if(WIN32)
  # Windows uses ANGLE-based implementation
  set(PLATFORM_SOURCES
    "agus_maps_flutter_win.cpp"
    "agus_platform_win.cpp"
    "agus_angle_context_factory.cpp"
    "agus_gui_thread_win.cpp"
  )
elseif(ANDROID)
  # Android uses EGL/GLES implementation
  set(PLATFORM_SOURCES
    "agus_maps_flutter.cpp"
    "agus_platform.cpp"
    "agus_ogl.cpp"
    "agus_gui_thread.cpp"
  )
else()
  # Default (iOS/macOS uses Metal, handled separately in podspec)
  set(PLATFORM_SOURCES
    "agus_maps_flutter.cpp"
    "agus_platform.cpp"
    "agus_ogl.cpp"
    "agus_gui_thread.cpp"
  )
endif()

add_library(agus_maps_flutter SHARED
  ${PLATFORM_SOURCES}
)

set_target_properties(agus_maps_flutter PROPERTIES
  OUTPUT_NAME "agus_maps_flutter"
)

# ============================================================================
# Include Directories
# ============================================================================
if(USE_PREBUILT_COMAPS AND COMAPS_HEADERS_DIR)
  # External consumer: use downloaded headers
  # Note: Include paths must match iOS podspec HEADER_SEARCH_PATHS
  target_include_directories(agus_maps_flutter PRIVATE
    "${COMAPS_HEADERS_DIR}/comaps"
    "${COMAPS_HEADERS_DIR}/comaps/libs"
    "${COMAPS_HEADERS_DIR}/comaps/3party"
    "${COMAPS_HEADERS_DIR}/comaps/3party/boost"
    "${COMAPS_HEADERS_DIR}/comaps/3party/utfcpp/source"
    "${COMAPS_HEADERS_DIR}/comaps/3party/glm"
    "${COMAPS_HEADERS_DIR}/comaps/3party/pugixml/pugixml/src"
    "${COMAPS_HEADERS_DIR}/comaps/3party/jansson/jansson/src"
    "${COMAPS_HEADERS_DIR}/comaps/3party/jansson"
    "${COMAPS_HEADERS_DIR}/comaps/3party/expat/expat/lib"
    "${COMAPS_HEADERS_DIR}/comaps/3party/icu/icu/source/common"
    "${COMAPS_HEADERS_DIR}/comaps/3party/icu/icu/source/i18n"
    "${COMAPS_HEADERS_DIR}/comaps/3party/freetype/include"
    "${COMAPS_HEADERS_DIR}/comaps/3party/harfbuzz/harfbuzz/src"
    "${COMAPS_HEADERS_DIR}/comaps/3party/minizip/minizip"
    "${COMAPS_HEADERS_DIR}/comaps/3party/protobuf/protobuf/src"
  )
  message(STATUS "Using headers from: ${COMAPS_HEADERS_DIR}/comaps")
else()
  # In-repo build: use thirdparty source headers
  # Note: Include paths must match iOS podspec HEADER_SEARCH_PATHS
  target_include_directories(agus_maps_flutter PRIVATE
    "../thirdparty/comaps"
    "../thirdparty/comaps/libs"
    "../thirdparty/comaps/3party"
    "../thirdparty/comaps/3party/boost"
    "../thirdparty/comaps/3party/utfcpp/source"
    "../thirdparty/comaps/3party/glm"
    "../thirdparty/comaps/3party/pugixml/pugixml/src"
    "../thirdparty/comaps/3party/jansson/jansson/src"
    "../thirdparty/comaps/3party/jansson"
    "../thirdparty/comaps/3party/expat/expat/lib"
    "../thirdparty/comaps/3party/icu/icu/source/common"
    "../thirdparty/comaps/3party/icu/icu/source/i18n"
    "../thirdparty/comaps/3party/freetype/include"
    "../thirdparty/comaps/3party/harfbuzz/harfbuzz/src"
    "../thirdparty/comaps/3party/minizip/minizip"
    "../thirdparty/comaps/3party/protobuf/protobuf/src"
    "../thirdparty/comaps/android/sdk/src/main/cpp"
    "../thirdparty/comaps/3party/succinct"
  )
  
  # Boost modular layout: headers are at libs/*/include/boost/
  if(WIN32)
    set(BOOST_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/comaps/3party/boost")
    target_include_directories(agus_maps_flutter PRIVATE
      "${BOOST_ROOT_DIR}/libs/algorithm/include"
      "${BOOST_ROOT_DIR}/libs/align/include"
      "${BOOST_ROOT_DIR}/libs/any/include"
      "${BOOST_ROOT_DIR}/libs/array/include"
      "${BOOST_ROOT_DIR}/libs/assert/include"
      "${BOOST_ROOT_DIR}/libs/atomic/include"
      "${BOOST_ROOT_DIR}/libs/bind/include"
      "${BOOST_ROOT_DIR}/libs/chrono/include"
      "${BOOST_ROOT_DIR}/libs/circular_buffer/include"
      "${BOOST_ROOT_DIR}/libs/concept_check/include"
      "${BOOST_ROOT_DIR}/libs/config/include"
      "${BOOST_ROOT_DIR}/libs/container/include"
      "${BOOST_ROOT_DIR}/libs/container_hash/include"
      "${BOOST_ROOT_DIR}/libs/conversion/include"
      "${BOOST_ROOT_DIR}/libs/core/include"
      "${BOOST_ROOT_DIR}/libs/date_time/include"
      "${BOOST_ROOT_DIR}/libs/describe/include"
      "${BOOST_ROOT_DIR}/libs/detail/include"
      "${BOOST_ROOT_DIR}/libs/endian/include"
      "${BOOST_ROOT_DIR}/libs/exception/include"
      "${BOOST_ROOT_DIR}/libs/function/include"
      "${BOOST_ROOT_DIR}/libs/function_types/include"
      "${BOOST_ROOT_DIR}/libs/functional/include"
      "${BOOST_ROOT_DIR}/libs/fusion/include"
      "${BOOST_ROOT_DIR}/libs/geometry/include"
      "${BOOST_ROOT_DIR}/libs/integer/include"
      "${BOOST_ROOT_DIR}/libs/intrusive/include"
      "${BOOST_ROOT_DIR}/libs/io/include"
      "${BOOST_ROOT_DIR}/libs/iterator/include"
      "${BOOST_ROOT_DIR}/libs/lambda/include"
      "${BOOST_ROOT_DIR}/libs/lexical_cast/include"
      "${BOOST_ROOT_DIR}/libs/math/include"
      "${BOOST_ROOT_DIR}/libs/move/include"
      "${BOOST_ROOT_DIR}/libs/mp11/include"
      "${BOOST_ROOT_DIR}/libs/mpl/include"
      "${BOOST_ROOT_DIR}/libs/multi_index/include"
      "${BOOST_ROOT_DIR}/libs/numeric/conversion/include"
      "${BOOST_ROOT_DIR}/libs/optional/include"
      "${BOOST_ROOT_DIR}/libs/polygon/include"
      "${BOOST_ROOT_DIR}/libs/pool/include"
      "${BOOST_ROOT_DIR}/libs/predef/include"
      "${BOOST_ROOT_DIR}/libs/preprocessor/include"
      "${BOOST_ROOT_DIR}/libs/proto/include"
      "${BOOST_ROOT_DIR}/libs/qvm/include"
      "${BOOST_ROOT_DIR}/libs/random/include"
      "${BOOST_ROOT_DIR}/libs/range/include"
      "${BOOST_ROOT_DIR}/libs/ratio/include"
      "${BOOST_ROOT_DIR}/libs/rational/include"
      "${BOOST_ROOT_DIR}/libs/regex/include"
      "${BOOST_ROOT_DIR}/libs/serialization/include"
      "${BOOST_ROOT_DIR}/libs/smart_ptr/include"
      "${BOOST_ROOT_DIR}/libs/spirit/include"
      "${BOOST_ROOT_DIR}/libs/stacktrace/include"
      "${BOOST_ROOT_DIR}/libs/static_assert/include"
      "${BOOST_ROOT_DIR}/libs/system/include"
      "${BOOST_ROOT_DIR}/libs/throw_exception/include"
      "${BOOST_ROOT_DIR}/libs/tokenizer/include"
      "${BOOST_ROOT_DIR}/libs/tti/include"
      "${BOOST_ROOT_DIR}/libs/tuple/include"
      "${BOOST_ROOT_DIR}/libs/type_index/include"
      "${BOOST_ROOT_DIR}/libs/type_traits/include"
      "${BOOST_ROOT_DIR}/libs/typeof/include"
      "${BOOST_ROOT_DIR}/libs/unordered/include"
      "${BOOST_ROOT_DIR}/libs/utility/include"
      "${BOOST_ROOT_DIR}/libs/uuid/include"
      "${BOOST_ROOT_DIR}/libs/variant/include"
      "${BOOST_ROOT_DIR}/libs/variant2/include"
      "${BOOST_ROOT_DIR}/libs/winapi/include"
    )
  endif()
endif()

# ============================================================================
# Link Libraries
# ============================================================================
if(USE_PREBUILT_COMAPS)
  # External consumer: link against pre-built static libraries
  # The pre-built libraries are organized by ABI in PREBUILT_DIR
  if(ANDROID)
    set(PREBUILT_ABI_DIR "${PREBUILT_DIR}/${ANDROID_ABI}")
    
    if(EXISTS "${PREBUILT_ABI_DIR}/libagus_maps_flutter.so")
      # If we have the full shared library, just use it directly
      # This is the expected case for pre-built distribution
      message(STATUS "Found pre-built shared library at ${PREBUILT_ABI_DIR}")
      
      # Import the pre-built shared library
      add_library(prebuilt_agus_maps SHARED IMPORTED)
      set_target_properties(prebuilt_agus_maps PROPERTIES
        IMPORTED_LOCATION "${PREBUILT_ABI_DIR}/libagus_maps_flutter.so"
      )
      
      # For pre-built mode, we just need to copy the library
      # The SHARED library declaration above will handle linking
      target_link_libraries(agus_maps_flutter PRIVATE prebuilt_agus_maps)
    else()
      message(FATAL_ERROR "Pre-built library not found at ${PREBUILT_ABI_DIR}")
    endif()
  endif()
else()
  # In-repo build: link against built CoMaps libraries
  target_link_libraries(agus_maps_flutter
    PRIVATE
    map
    platform
    coding
    geometry
    base
    drape
    drape_frontend
  )
endif()

target_compile_definitions(agus_maps_flutter PUBLIC DART_SHARED_LIB)

# Match CoMaps build mode definitions
# On Windows/MSVC, this is handled globally above with generator expressions
# to properly work with multi-config builds and avoid Debug/Release conflicts
if (NOT MSVC)
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(agus_maps_flutter PRIVATE DEBUG)
  else()
    target_compile_definitions(agus_maps_flutter PRIVATE RELEASE NDEBUG)
  endif()
endif()

if (ANDROID)
  find_library(log-lib log)
  find_library(android-lib android)
  find_library(EGL-lib EGL)
  find_library(GLESv3-lib GLESv3)
  
  message(STATUS "Found log-lib: ${log-lib}")
  message(STATUS "Found android-lib: ${android-lib}")
  message(STATUS "Found EGL-lib: ${EGL-lib}")
  message(STATUS "Found GLESv3-lib: ${GLESv3-lib}")

  target_link_libraries(agus_maps_flutter PRIVATE 
    -llog
    -landroid
    -lEGL
    -lGLESv3
  )
  # Support Android 15 16k page size
  target_link_options(agus_maps_flutter PRIVATE "-Wl,-z,max-page-size=16384")
  # Allow our stubs to override libplatform.a definitions
  target_link_options(agus_maps_flutter PRIVATE "-Wl,--allow-multiple-definition")
endif()

# ============================================================================
# Windows Platform Configuration (ANGLE for OpenGL ES -> DirectX 11)
# ============================================================================
if (WIN32)
  message(STATUS "=== Configuring for Windows x64 with ANGLE ===")
  
  # Windows-specific compile definitions
  target_compile_definitions(agus_maps_flutter PRIVATE
    PLATFORM_DESKTOP=1
    PLATFORM_WIN=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _USE_MATH_DEFINES
    # Suppress MSVC warnings for POSIX functions
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
  )
  
  # MSVC-specific compile options
  if (MSVC)
    target_compile_options(agus_maps_flutter PRIVATE
      /utf-8
      /W3
      # Disable specific warnings that CoMaps triggers frequently
      /wd4244  # conversion from 'type1' to 'type2', possible loss of data
      /wd4267  # conversion from 'size_t' to 'type', possible loss of data
      /wd4996  # deprecated POSIX names
      /wd4305  # truncation from 'double' to 'float'
      /wd4018  # signed/unsigned mismatch
    )
  endif()
  
  # Find ANGLE via vcpkg (installed via vcpkg.json manifest)
  find_package(unofficial-angle CONFIG REQUIRED)
  if (unofficial-angle_FOUND)
    message(STATUS "Found ANGLE via vcpkg")
    target_link_libraries(agus_maps_flutter PRIVATE
      unofficial::angle::libEGL
      unofficial::angle::libGLESv2
    )
  else()
    message(FATAL_ERROR "ANGLE not found. Run: vcpkg install --triplet x64-windows")
  endif()
  
  # Link against Windows system libraries
  target_link_libraries(agus_maps_flutter PRIVATE
    d3d11
    dxgi
    user32
    gdi32
    shell32
    ole32
  )
  
  message(STATUS "ANGLE libraries linked successfully")
endif()
