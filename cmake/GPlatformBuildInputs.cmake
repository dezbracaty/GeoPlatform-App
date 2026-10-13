include_guard(GLOBAL)

# Cache/command-line values take precedence over environment defaults.
foreach(_input GPLATFORM_TARGET_ARCH CMAKE_FETCH_CACHE CMAKE_TOOLCHAIN_FILE
        CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_MAKE_PROGRAM
        Python3_EXECUTABLE GIT_EXECUTABLE PERL_EXECUTABLE Qt6_DIR)
    if(NOT DEFINED ${_input} AND DEFINED ENV{${_input}} AND NOT "$ENV{${_input}}" STREQUAL "")
        set(${_input} "$ENV{${_input}}" CACHE STRING "Build input: ${_input}")
    endif()
endforeach()
foreach(_path_input CMAKE_FETCH_CACHE CMAKE_TOOLCHAIN_FILE
        Python3_EXECUTABLE GIT_EXECUTABLE PERL_EXECUTABLE Qt6_DIR)
    if(${_path_input})
        file(TO_CMAKE_PATH "${${_path_input}}" ${_path_input})
    endif()
endforeach()
if(CMAKE_HOST_WIN32)
    if(NOT Qt6_DIR)
        message(FATAL_ERROR
            "Qt6_DIR is required on Windows. Set it to the directory containing Qt6Config.cmake.\n"
            "VS Code: set cmake.configureEnvironment.Qt6_DIR in .vscode/settings.json.\n"
            "CLI: set the Qt6_DIR environment variable or pass -DQt6_DIR=\"<Qt>/lib/cmake/Qt6\".")
    endif()
    if(NOT IS_DIRECTORY "${Qt6_DIR}" OR NOT EXISTS "${Qt6_DIR}/Qt6Config.cmake"
            OR IS_DIRECTORY "${Qt6_DIR}/Qt6Config.cmake")
        message(FATAL_ERROR
            "Qt6_DIR does not contain Qt6Config.cmake: ${Qt6_DIR}\n"
            "Select the matching MSVC Qt installation's lib/cmake/Qt6 directory.\n"
            "Update Qt6_DIR in .vscode/settings.json, the environment, or the CMake cache.")
    endif()
endif()
if(GPLATFORM_TARGET_ARCH AND CMAKE_HOST_APPLE)
    set(_requested_arch "${GPLATFORM_TARGET_ARCH}")
    if(_requested_arch STREQUAL "x64")
        set(_requested_arch x86_64)
    endif()
    if(CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL _requested_arch)
        message(FATAL_ERROR "GPLATFORM_TARGET_ARCH conflicts with CMAKE_OSX_ARCHITECTURES.")
    endif()
    set(CMAKE_OSX_ARCHITECTURES "${_requested_arch}" CACHE STRING "Target architecture")
endif()

if(NOT CMAKE_FETCH_CACHE)
    if(CMAKE_HOST_WIN32)
        set(_home "$ENV{USERPROFILE}")
    else()
        set(_home "$ENV{HOME}")
    endif()
    if(NOT _home)
        message(FATAL_ERROR "Cannot determine user home. Set CMAKE_FETCH_CACHE to a writable directory.")
    endif()
    set(CMAKE_FETCH_CACHE "${_home}/.cmake_fetch_cache" CACHE PATH "Dependency cache directory" FORCE)
endif()
cmake_path(ABSOLUTE_PATH CMAKE_FETCH_CACHE BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)
set(ENV{CMAKE_FETCH_CACHE} "${CMAKE_FETCH_CACHE}")

if(CMAKE_TOOLCHAIN_FILE AND NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "CMAKE_TOOLCHAIN_FILE does not exist: ${CMAKE_TOOLCHAIN_FILE}")
endif()
if(CMAKE_GENERATOR STREQUAL "Ninja")
    if(NOT CMAKE_MAKE_PROGRAM)
        find_program(CMAKE_MAKE_PROGRAM NAMES ninja)
    endif()
    if(NOT CMAKE_MAKE_PROGRAM OR NOT EXISTS "${CMAKE_MAKE_PROGRAM}")
        message(FATAL_ERROR "Ninja was not found. Install Ninja on PATH or set CMAKE_MAKE_PROGRAM to its executable.")
    endif()
    execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
        RESULT_VARIABLE _ninja_result OUTPUT_QUIET ERROR_QUIET TIMEOUT 10)
    if(NOT "${_ninja_result}" STREQUAL "0")
        message(FATAL_ERROR "CMAKE_MAKE_PROGRAM cannot run: ${CMAKE_MAKE_PROGRAM}")
    endif()
endif()
