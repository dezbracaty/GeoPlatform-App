# FetchCacheHelper.cmake - canonical third-party cache layout

cmake_minimum_required(VERSION 3.16)
include_guard(GLOBAL)

# Resolve one architecture tag for binary build/install artifacts. Source trees
# and archives remain architecture-neutral at the package root.
function(gplatform_get_fetch_cache_arch OUT_VAR)
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES _arch_count)
        if(_arch_count GREATER 1)
            message(FATAL_ERROR
                "Universal dependency builds are not supported "
                "(CMAKE_OSX_ARCHITECTURES='${CMAKE_OSX_ARCHITECTURES}'). "
                "Build each architecture separately and merge externally.")
        endif()
        set(_arch "${CMAKE_OSX_ARCHITECTURES}")
    elseif(CMAKE_GENERATOR_PLATFORM)
        set(_arch "${CMAKE_GENERATOR_PLATFORM}")
    elseif(CMAKE_SYSTEM_PROCESSOR)
        set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
    elseif(WIN32 AND DEFINED ENV{VSCMD_ARG_TGT_ARCH}
           AND NOT "$ENV{VSCMD_ARG_TGT_ARCH}" STREQUAL "")
        set(_arch "$ENV{VSCMD_ARG_TGT_ARCH}")
    elseif(WIN32 AND DEFINED ENV{PROCESSOR_ARCHITECTURE}
           AND NOT "$ENV{PROCESSOR_ARCHITECTURE}" STREQUAL "")
        set(_arch "$ENV{PROCESSOR_ARCHITECTURE}")
    else()
        message(FATAL_ERROR
            "Cannot determine the target architecture for CMAKE_FETCH_CACHE")
    endif()

    string(TOLOWER "${_arch}" _arch)
    if(WIN32 AND _arch STREQUAL "amd64")
        set(_arch "x64")
    endif()
    string(REGEX REPLACE "[^a-z0-9_.-]+" "-" _arch "${_arch}")
    set(${OUT_VAR} "${_arch}" PARENT_SCOPE)
endfunction()

# Canonical layout:
#   <cache>/<name>-v<version>/
#     <archive>
#     src/
#     <arch>/
#       build-<config>/
#       install-<config>/
function(gplatform_resolve_fetch_cache PREFIX LIB_NAME LIB_VERSION BUILD_TYPE)
    if(NOT DEFINED ENV{CMAKE_FETCH_CACHE}
       OR "$ENV{CMAKE_FETCH_CACHE}" STREQUAL "")
        message(FATAL_ERROR
            "CMAKE_FETCH_CACHE is not set. Set it before configuring the project.")
    endif()

    file(TO_CMAKE_PATH "$ENV{CMAKE_FETCH_CACHE}" _fetch_cache_dir)
    gplatform_get_fetch_cache_arch(_fetch_cache_arch)

    set(_cache_dir "${_fetch_cache_dir}/${LIB_NAME}-v${LIB_VERSION}")
    set(_arch_dir "${_cache_dir}/${_fetch_cache_arch}")
    set(_forward_args)
    if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja")
        # Child projects need the same raw /showIncludes prefix detection as
        # the App; CMake's decoded prefix can differ from Chinese MSVC output.
        get_filename_component(_msvc_dependency_hook
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../GPlatformMsvcDependencies.cmake" ABSOLUTE)
        list(APPEND _forward_args "-DCMAKE_PROJECT_INCLUDE=${_msvc_dependency_hook}")
    endif()
    if(CMAKE_GENERATOR STREQUAL "Ninja" AND CMAKE_MAKE_PROGRAM)
        list(APPEND _forward_args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
    endif()
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND _forward_args
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    if(CMAKE_C_COMPILER)
        list(APPEND _forward_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
    endif()
    if(CMAKE_CXX_COMPILER)
        list(APPEND _forward_args
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    endif()
    if(APPLE)
        list(APPEND _forward_args
            "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
        if(CMAKE_OSX_DEPLOYMENT_TARGET)
            list(APPEND _forward_args
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        endif()
        if(CMAKE_OSX_SYSROOT)
            list(APPEND _forward_args
                "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
        endif()
    endif()

    file(MAKE_DIRECTORY "${_cache_dir}" "${_arch_dir}")
    # Header-only and official prebuilt packages do not use our source compiler.
    if(COMMAND gplatform_check_cache_contract AND
       NOT LIB_NAME MATCHES "^(filament|cereal|eigen3)$")
        gplatform_check_cache_contract("${_arch_dir}/build-${BUILD_TYPE}")
    endif()


    set(FETCH_CACHE_DIR "${_fetch_cache_dir}" PARENT_SCOPE)
    set(FETCH_CACHE_ARCH "${_fetch_cache_arch}" PARENT_SCOPE)
    set(FETCH_CACHE_FORWARD_ARGS "${_forward_args}" PARENT_SCOPE)
    set(${PREFIX}_CACHE_DIR "${_cache_dir}" PARENT_SCOPE)
    set(${PREFIX}_ARCH_DIR "${_arch_dir}" PARENT_SCOPE)
    set(${PREFIX}_SOURCE_DIR "${_cache_dir}/src" PARENT_SCOPE)
    set(${PREFIX}_BUILD_DIR
        "${_arch_dir}/build-${BUILD_TYPE}" PARENT_SCOPE)
    set(${PREFIX}_INSTALL_DIR
        "${_arch_dir}/install-${BUILD_TYPE}" PARENT_SCOPE)
endfunction()
