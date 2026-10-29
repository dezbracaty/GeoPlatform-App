# Configure a lightweight build driver. Each CPU retains its own real project
# and dependency cache; no multi-architecture third-party build is requested.
if(NOT CMAKE_HOST_APPLE)
    message(FATAL_ERROR "Universal application presets require macOS")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/GPlatformBuildInputs.cmake")
project(GPlatformUniversal LANGUAGES NONE)
if(NOT CMAKE_BUILD_TYPE MATCHES "^(Release|Debug)$")
    message(FATAL_ERROR "Select universal_release or universal_debug")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_package(Git REQUIRED)
if(NOT Qt6_DIR)
    find_path(Qt6_DIR Qt6Config.cmake HINTS ${CMAKE_PREFIX_PATH}
        PATH_SUFFIXES lib/cmake/Qt6)
endif()
if(NOT EXISTS "${Qt6_DIR}/Qt6Config.cmake")
    message(FATAL_ERROR "Set cmake.configureEnvironment.Qt6_DIR to the selected Qt lib/cmake/Qt6 directory")
endif()

set(_universal_target GPlatform)
if(EXISTS "${CMAKE_SOURCE_DIR}/App/CMakeLists.txt")
    set(_universal_target gplatform_build)
endif()
set(_universal_environment)
foreach(_input CMAKE_MAKE_PROGRAM CMAKE_FETCH_CACHE Python3_EXECUTABLE
        GIT_EXECUTABLE PERL_EXECUTABLE CMAKE_TOOLCHAIN_FILE CMAKE_C_COMPILER CMAKE_CXX_COMPILER)
    if(DEFINED ${_input} AND NOT "${${_input}}" STREQUAL "")
        list(APPEND _universal_environment "${_input}=${${_input}}")
    endif()
endforeach()
add_custom_target(GPlatform ALL
    COMMAND "${CMAKE_COMMAND}" -E env ${_universal_environment}
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/build_macos_universal.py"
        --source-dir "${CMAKE_SOURCE_DIR}"
        --cmake "${CMAKE_COMMAND}"
        --build-dir "${CMAKE_BINARY_DIR}"
        --arm64-build-dir "${CMAKE_SOURCE_DIR}/build-arm64-${CMAKE_BUILD_TYPE}"
        --x86_64-build-dir "${CMAKE_SOURCE_DIR}/build-x86_64-${CMAKE_BUILD_TYPE}"
        --qt-dir "${Qt6_DIR}" --config "${CMAKE_BUILD_TYPE}"
        --target "${_universal_target}"
    COMMENT "Build, deploy and verify macOS arm64 + x86_64 ${CMAKE_BUILD_TYPE}"
    USES_TERMINAL VERBATIM)
add_custom_target(gplatform_build DEPENDS GPlatform)
message(STATUS "Universal App: ${CMAKE_BINARY_DIR}/GPlatform.app")
message(STATUS "Thin builds: build-arm64-${CMAKE_BUILD_TYPE}, build-x86_64-${CMAKE_BUILD_TYPE}")
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    message(STATUS "Compile commands are provided by the architecture-specific build directories")
endif()
