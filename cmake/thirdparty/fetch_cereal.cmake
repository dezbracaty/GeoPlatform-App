cmake_minimum_required(VERSION 3.16)
include_guard(GLOBAL)

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

set(CEREAL_VERSION "1.3.0" CACHE STRING "cereal version to use")
set(CEREAL_SHA256
    "71642cb54658e98c8f07a0f0d08bf9766f1c3771496936f6014169d3726d9657")

# Reuse the cache populated by the existing libslicer dependency build when it
# is available. cereal is header-only, so Release headers are valid for every
# host build configuration.
gplatform_resolve_fetch_cache(CEREAL "cereal" "${CEREAL_VERSION}" "Release")
set(_cereal_config_dir "${CEREAL_INSTALL_DIR}/share/cmake/cereal")

if(EXISTS "${_cereal_config_dir}/cereal-config.cmake")
    find_package(cereal CONFIG REQUIRED
        PATHS "${_cereal_config_dir}"
        NO_DEFAULT_PATH)
elseif(EXISTS "${CEREAL_SOURCE_DIR}/CMakeLists.txt")
    set(JUST_INSTALL_CEREAL ON CACHE BOOL "" FORCE)
    set(SKIP_PERFORMANCE_COMPARISON ON CACHE BOOL "" FORCE)
    add_subdirectory(
        "${CEREAL_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/_deps/cereal-build"
        EXCLUDE_FROM_ALL)
else()
    set(_cereal_content_url
        "https://github.com/USCiLab/cereal/archive/refs/tags/v${CEREAL_VERSION}.zip")
    FetchContent_GetProperties(cereal)
    if(NOT cereal_POPULATED AND NOT FETCHCONTENT_SOURCE_DIR_CEREAL AND
       NOT FETCHCONTENT_FULLY_DISCONNECTED)
        set(_cereal_archive "${CEREAL_CACHE_DIR}/cereal-${CEREAL_VERSION}.zip")
        gplatform_download("${_cereal_content_url}" "${_cereal_archive}"
            EXPECTED_HASH "SHA256=${CEREAL_SHA256}"
            STATUS _cereal_download_status LOG _cereal_download_log SHOW_PROGRESS)
        list(GET _cereal_download_status 0 _cereal_download_code)
        if(NOT _cereal_download_code EQUAL 0)
            message(FATAL_ERROR "cereal download failed: ${_cereal_download_log}")
        endif()
        set(_cereal_content_url "${_cereal_archive}")
    endif()
    FetchContent_Declare(cereal
        URL "${_cereal_content_url}"
        URL_HASH "SHA256=${CEREAL_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(JUST_INSTALL_CEREAL ON CACHE BOOL "" FORCE)
    set(SKIP_PERFORMANCE_COMPARISON ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(cereal)
endif()

if(TARGET cereal AND NOT TARGET cereal::cereal)
    add_library(cereal::cereal ALIAS cereal)
endif()
if(TARGET cereal::cereal AND NOT TARGET cereal)
    # Build-tree SDK exports resolve source aliases to the concrete cereal name.
    # Keep that name available when this consumer instead loads the installed package.
    add_library(cereal ALIAS cereal::cereal)
endif()

if(NOT TARGET cereal::cereal)
    message(FATAL_ERROR "cereal target was not created")
endif()

unset(_cereal_config_dir)
