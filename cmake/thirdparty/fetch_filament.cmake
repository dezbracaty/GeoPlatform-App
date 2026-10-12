cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

# Filament version config - can be overridden from the command line.
if(NOT DEFINED FILAMENT_VERSION)
    set(FILAMENT_VERSION "1.75.0" CACHE STRING "Filament version to use")
endif()

# Official release assets and SHA256 values published by Google Filament.
set(FILAMENT_KNOWN_SHA256_1.75.0_mac
    "731b5fe71844e82e2ee45b891b05b9c07599fabccd297033275d90baefe97021")
set(FILAMENT_KNOWN_SHA256_1.75.0_windows
    "36b3f90e3551e683fd1567cf36fc01eddd284a774f64ddfaeca624ccdc657e03")
set(FILAMENT_KNOWN_SHA256_1.75.0_linux
    "c5d2e0f692e5fb98ed029a5a3a52c8174660d02844d5db5a804dc5264bbab6d1")
set(FILAMENT_KNOWN_SHA256_1.75.0_arm-linux
    "6a312705c86890840d6cdb47380524f8a96495aeafa56ae05726a630e25594db")
set(FILAMENT_KNOWN_SHA256_1.75.0_ios
    "3b1846efcfbf6f99014e6ceeecc3449c521aad0ea53758eb9ecb7ba83242cc18")

# Official repository assets consumed by the Filament integration showcase.
# Keep these hashes versioned together with the platform package so every
# upstream Filament input has one download and verification entry point.
set(FILAMENT_KNOWN_MONKEY_SHA256_1.75.0
    "7b754119aa286ff618b9ac30038c8aa9a093e2820242913728c8b20d8000ca88")
set(FILAMENT_KNOWN_PILLARS_SHA256_1.75.0
    "e4b5a3e432eab8104c9b8e452357655a672774516c7a36f5beab07f460588be8")

if(DEFINED FILAMENT_BUILD_TYPE)
    set(FILAMENT_ACTUAL_BUILD_TYPE "${FILAMENT_BUILD_TYPE}")
elseif(CMAKE_BUILD_TYPE)
    set(FILAMENT_ACTUAL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
    set(FILAMENT_ACTUAL_BUILD_TYPE "Release")
endif()

gplatform_resolve_fetch_cache(
    FILAMENT "filament" "${FILAMENT_VERSION}" "${FILAMENT_ACTUAL_BUILD_TYPE}")

# Select exactly one official platform distribution.
if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(FILAMENT_ASSET_PLATFORM "ios")
elseif(APPLE)
    set(FILAMENT_ASSET_PLATFORM "mac")
elseif(WIN32)
    set(FILAMENT_ASSET_PLATFORM "windows")
elseif(UNIX)
    if(FETCH_CACHE_ARCH MATCHES "^(arm64|aarch64)$")
        set(FILAMENT_ASSET_PLATFORM "arm-linux")
    elseif(FETCH_CACHE_ARCH MATCHES "^(x64|x86_64|amd64)$")
        set(FILAMENT_ASSET_PLATFORM "linux")
    else()
        message(FATAL_ERROR
            "Filament ${FILAMENT_VERSION} has no registered Linux asset for "
            "architecture '${FETCH_CACHE_ARCH}'")
    endif()
else()
    message(FATAL_ERROR
        "Filament ${FILAMENT_VERSION} has no registered release asset for "
        "platform '${CMAKE_SYSTEM_NAME}'")
endif()

set(FILAMENT_SHA_VARIABLE
    "FILAMENT_KNOWN_SHA256_${FILAMENT_VERSION}_${FILAMENT_ASSET_PLATFORM}")
if(NOT DEFINED ${FILAMENT_SHA_VARIABLE})
    message(FATAL_ERROR
        "Filament ${FILAMENT_VERSION}/${FILAMENT_ASSET_PLATFORM} has no "
        "registered SHA256 in fetch_filament.cmake")
endif()
set(FILAMENT_EXPECTED_SHA256 "${${FILAMENT_SHA_VARIABLE}}")

set(FILAMENT_ASSET_NAME
    "filament-v${FILAMENT_VERSION}-${FILAMENT_ASSET_PLATFORM}.tgz")
set(FILAMENT_ARCHIVE_FILE
    "${FILAMENT_CACHE_DIR}/${FILAMENT_ASSET_NAME}")
set(FILAMENT_DOWNLOAD_URL
    "https://github.com/google/filament/releases/download/v${FILAMENT_VERSION}/${FILAMENT_ASSET_NAME}")

message(STATUS "")
message(STATUS "Filament release cache manager")
message(STATUS "  version: ${FILAMENT_VERSION}")
message(STATUS "  platform asset: ${FILAMENT_ASSET_PLATFORM}")
message(STATUS "  asset: ${FILAMENT_ASSET_NAME}")
message(STATUS "  cache root: ${FETCH_CACHE_DIR}")
message(STATUS "  target arch: ${FETCH_CACHE_ARCH}")
message(STATUS "  archive: ${FILAMENT_ARCHIVE_FILE}")
message(STATUS "  install dir: ${FILAMENT_INSTALL_DIR}")

if(MSVC)
    # Match CMake's default DLL CRT, or an explicitly selected static CRT.
    if(NOT CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
        set(FILAMENT_MSVC_RUNTIME md)
    else()
        set(FILAMENT_MSVC_RUNTIME mt)
    endif()
endif()

function(_gplatform_find_filament_library_dir OUTPUT_VARIABLE)
    set(_arch_candidates "${FETCH_CACHE_ARCH}")
    if(FETCH_CACHE_ARCH STREQUAL "x64")
        list(APPEND _arch_candidates x86_64)
    elseif(FETCH_CACHE_ARCH STREQUAL "x86_64")
        list(APPEND _arch_candidates x64)
    elseif(FETCH_CACHE_ARCH STREQUAL "aarch64")
        list(APPEND _arch_candidates arm64)
    elseif(FETCH_CACHE_ARCH STREQUAL "arm64")
        list(APPEND _arch_candidates aarch64)
    endif()

    foreach(_arch IN LISTS _arch_candidates)
        set(_candidate "${FILAMENT_INSTALL_DIR}/lib/${_arch}")
        if(MSVC)
            string(APPEND _candidate "/${FILAMENT_MSVC_RUNTIME}")
        endif()
        if(EXISTS
           "${_candidate}/${CMAKE_STATIC_LIBRARY_PREFIX}filament${CMAKE_STATIC_LIBRARY_SUFFIX}")
            set(${OUTPUT_VARIABLE} "${_candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    file(GLOB _core_libraries
        "${FILAMENT_INSTALL_DIR}/lib/*/${CMAKE_STATIC_LIBRARY_PREFIX}filament${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${FILAMENT_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}filament${CMAKE_STATIC_LIBRARY_SUFFIX}")
    if(_core_libraries)
        list(GET _core_libraries 0 _core_library)
        get_filename_component(_library_dir "${_core_library}" DIRECTORY)
        set(${OUTPUT_VARIABLE} "${_library_dir}" PARENT_SCOPE)
    else()
        set(${OUTPUT_VARIABLE} "" PARENT_SCOPE)
    endif()
endfunction()

_gplatform_find_filament_library_dir(FILAMENT_LIBRARY_DIR)
set(HAS_INSTALL FALSE)
set(HAS_ZIP FALSE)

if(EXISTS "${FILAMENT_INSTALL_DIR}/include/filament/Engine.h" AND
   FILAMENT_LIBRARY_DIR)
    set(HAS_INSTALL TRUE)
    message(STATUS "Filament: cached installation found")
endif()

if(EXISTS "${FILAMENT_ARCHIVE_FILE}")
    file(SHA256 "${FILAMENT_ARCHIVE_FILE}" FILAMENT_ACTUAL_SHA256)
    if(FILAMENT_ACTUAL_SHA256 STREQUAL FILAMENT_EXPECTED_SHA256)
        set(HAS_ZIP TRUE)
        message(STATUS "Filament: cached archive found (SHA256 verified)")
    else()
        message(WARNING
            "Filament cached archive SHA256 mismatch; downloading again\n"
            "  expected: ${FILAMENT_EXPECTED_SHA256}\n"
            "  actual: ${FILAMENT_ACTUAL_SHA256}")
        file(REMOVE "${FILAMENT_ARCHIVE_FILE}")
    endif()
endif()

if(HAS_INSTALL)
    set(FILAMENT_STATUS "INSTALLED")
elseif(HAS_ZIP)
    set(FILAMENT_STATUS "ZIP_ONLY")
else()
    set(FILAMENT_STATUS "NONE")
endif()
message(STATUS
    "Filament status: Install=${HAS_INSTALL}, Archive=${HAS_ZIP}")
message(STATUS "Filament strategy: ${FILAMENT_STATUS}")

if(FILAMENT_STATUS STREQUAL "NONE")
    message(STATUS "Filament: downloading official ${FILAMENT_ASSET_PLATFORM} package")
    message(STATUS "  URL: ${FILAMENT_DOWNLOAD_URL}")
    file(DOWNLOAD
        "${FILAMENT_DOWNLOAD_URL}"
        "${FILAMENT_ARCHIVE_FILE}"
        EXPECTED_HASH "SHA256=${FILAMENT_EXPECTED_SHA256}"
        STATUS FILAMENT_DOWNLOAD_STATUS
        LOG FILAMENT_DOWNLOAD_LOG
        SHOW_PROGRESS)

    list(GET FILAMENT_DOWNLOAD_STATUS 0 FILAMENT_DOWNLOAD_STATUS_CODE)
    list(GET FILAMENT_DOWNLOAD_STATUS 1 FILAMENT_DOWNLOAD_STATUS_MESSAGE)
    if(NOT FILAMENT_DOWNLOAD_STATUS_CODE EQUAL 0)
        message(FATAL_ERROR
            "Filament download failed: ${FILAMENT_DOWNLOAD_STATUS_MESSAGE}\n"
            "${FILAMENT_DOWNLOAD_LOG}")
    endif()
    set(FILAMENT_STATUS "ZIP_ONLY")
endif()

if(FILAMENT_STATUS STREQUAL "ZIP_ONLY")
    set(FILAMENT_STAGING_DIR
        "${FILAMENT_ARCH_DIR}/extract-${FILAMENT_ASSET_PLATFORM}")
    file(REMOVE_RECURSE "${FILAMENT_STAGING_DIR}")
    file(MAKE_DIRECTORY "${FILAMENT_STAGING_DIR}")

    message(STATUS "Filament: extracting official platform package")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xzf "${FILAMENT_ARCHIVE_FILE}"
        WORKING_DIRECTORY "${FILAMENT_STAGING_DIR}"
        RESULT_VARIABLE FILAMENT_EXTRACT_RESULT
        ERROR_VARIABLE FILAMENT_EXTRACT_ERROR)
    if(NOT FILAMENT_EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Filament package extraction failed: ${FILAMENT_EXTRACT_ERROR}")
    endif()

    if(EXISTS "${FILAMENT_STAGING_DIR}/filament/include/filament/Engine.h")
        set(FILAMENT_EXTRACTED_ROOT "${FILAMENT_STAGING_DIR}/filament")
    elseif(EXISTS "${FILAMENT_STAGING_DIR}/include/filament/Engine.h")
        set(FILAMENT_EXTRACTED_ROOT "${FILAMENT_STAGING_DIR}")
    else()
        message(FATAL_ERROR
            "Filament package has an unexpected directory layout")
    endif()

    file(REMOVE_RECURSE "${FILAMENT_INSTALL_DIR}")
    if(FILAMENT_EXTRACTED_ROOT STREQUAL FILAMENT_STAGING_DIR)
        file(MAKE_DIRECTORY "${FILAMENT_INSTALL_DIR}")
        file(COPY "${FILAMENT_STAGING_DIR}/"
            DESTINATION "${FILAMENT_INSTALL_DIR}")
    else()
        file(RENAME "${FILAMENT_EXTRACTED_ROOT}" "${FILAMENT_INSTALL_DIR}")
    endif()
    file(REMOVE_RECURSE "${FILAMENT_STAGING_DIR}")

    _gplatform_find_filament_library_dir(FILAMENT_LIBRARY_DIR)
    if(NOT EXISTS "${FILAMENT_INSTALL_DIR}/include/filament/Engine.h" OR
       NOT FILAMENT_LIBRARY_DIR)
        message(FATAL_ERROR "Filament platform package installation is incomplete")
    endif()
endif()

# Fetch all additional official Filament inputs here as part of the same
# dependency flow. Renderer backend CMake files only consume the resolved
# paths and remain free of network/download implementation details.
set(FILAMENT_ASSET_CACHE_DIR "${FILAMENT_CACHE_DIR}/assets")
file(MAKE_DIRECTORY "${FILAMENT_ASSET_CACHE_DIR}")

function(_gplatform_fetch_filament_repository_asset
         OUTPUT_VARIABLE FILE_NAME REPOSITORY_PATH EXPECTED_SHA256)
    set(_destination "${FILAMENT_ASSET_CACHE_DIR}/${FILE_NAME}")
    set(_valid FALSE)
    if(EXISTS "${_destination}")
        file(SHA256 "${_destination}" _actual_sha256)
        if(_actual_sha256 STREQUAL EXPECTED_SHA256)
            set(_valid TRUE)
        else()
            file(REMOVE "${_destination}")
        endif()
    endif()

    if(NOT _valid)
        set(_url
            "https://api.github.com/repos/google/filament/contents/${REPOSITORY_PATH}?ref=v${FILAMENT_VERSION}")
        message(STATUS "Filament: downloading official asset ${FILE_NAME}")
        file(DOWNLOAD
            "${_url}"
            "${_destination}"
            EXPECTED_HASH "SHA256=${EXPECTED_SHA256}"
            HTTPHEADER "Accept: application/vnd.github.raw+json"
            HTTPHEADER "User-Agent: GPlatform-CMake"
            STATUS _download_status
            LOG _download_log
            SHOW_PROGRESS)
        list(GET _download_status 0 _status_code)
        list(GET _download_status 1 _status_message)
        if(NOT _status_code EQUAL 0)
            file(REMOVE "${_destination}")
            message(FATAL_ERROR
                "Filament asset download failed: ${_status_message}\n"
                "${_download_log}")
        endif()
    endif()

    set(${OUTPUT_VARIABLE} "${_destination}" PARENT_SCOPE)
endfunction()

set(_filament_monkey_sha_variable
    "FILAMENT_KNOWN_MONKEY_SHA256_${FILAMENT_VERSION}")
set(_filament_pillars_sha_variable
    "FILAMENT_KNOWN_PILLARS_SHA256_${FILAMENT_VERSION}")
if(NOT DEFINED ${_filament_monkey_sha_variable} OR
   NOT DEFINED ${_filament_pillars_sha_variable})
    message(FATAL_ERROR
        "Filament ${FILAMENT_VERSION} has no registered showcase asset hashes")
endif()

_gplatform_fetch_filament_repository_asset(
    FILAMENT_SHOWCASE_MONKEY_OBJ
    "monkey.obj"
    "assets/models/monkey/monkey.obj"
    "${${_filament_monkey_sha_variable}}")
_gplatform_fetch_filament_repository_asset(
    FILAMENT_SHOWCASE_PILLARS_HDR
    "pillars_2k.hdr"
    "third_party/environments/pillars_2k.hdr"
    "${${_filament_pillars_sha_variable}}")

# Filament distributions do not ship a CMake package config. Define imported
# targets once here so only renderer backend libraries need to know the
# concrete Filament dependency layout.
set(FILAMENT_STATIC_COMPONENTS
    filament
    backend
    basis_transcoder
    filabridge
    filaflat
    filameshio
    gltfio_core
    image
    ktxreader
    dracodec
    geometry
    meshoptimizer
    stb
    uberarchive
    uberzlib
    utils
    bluegl
    bluevk
    smol-v
    zstd)
foreach(FILAMENT_COMPONENT IN LISTS FILAMENT_STATIC_COMPONENTS)
    set(FILAMENT_COMPONENT_LIBRARY
        "${FILAMENT_LIBRARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${FILAMENT_COMPONENT}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    if(NOT EXISTS "${FILAMENT_COMPONENT_LIBRARY}")
        message(FATAL_ERROR
            "Filament package is missing ${FILAMENT_COMPONENT_LIBRARY}")
    endif()

    set(FILAMENT_COMPONENT_TARGET "Filament::${FILAMENT_COMPONENT}")
    if(NOT TARGET ${FILAMENT_COMPONENT_TARGET})
        add_library(${FILAMENT_COMPONENT_TARGET} STATIC IMPORTED GLOBAL)
        set_target_properties(${FILAMENT_COMPONENT_TARGET} PROPERTIES
            IMPORTED_LOCATION "${FILAMENT_COMPONENT_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FILAMENT_INSTALL_DIR}/include")
        if(MSVC)
            set(_debug_library
                "${FILAMENT_LIBRARY_DIR}d/${FILAMENT_COMPONENT}${CMAKE_STATIC_LIBRARY_SUFFIX}")
            if(NOT EXISTS "${_debug_library}")
                message(FATAL_ERROR "Filament package is missing ${_debug_library}")
            endif()
            set_target_properties(${FILAMENT_COMPONENT_TARGET} PROPERTIES
                IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
                IMPORTED_LOCATION_DEBUG "${_debug_library}"
                IMPORTED_LOCATION_RELEASE "${FILAMENT_COMPONENT_LIBRARY}"
                MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
                MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE)
        endif()
    endif()
endforeach()

# Filament's math and tsl components are header-only in the official desktop
# packages. Preserve their target-level dependency semantics without looking
# for static archives that the package does not contain.
set(FILAMENT_HEADER_ONLY_COMPONENTS
    math
    tsl)
foreach(FILAMENT_COMPONENT IN LISTS FILAMENT_HEADER_ONLY_COMPONENTS)
    set(FILAMENT_COMPONENT_TARGET "Filament::${FILAMENT_COMPONENT}")
    if(NOT TARGET ${FILAMENT_COMPONENT_TARGET})
        add_library(${FILAMENT_COMPONENT_TARGET} INTERFACE IMPORTED GLOBAL)
        set_target_properties(${FILAMENT_COMPONENT_TARGET} PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${FILAMENT_INSTALL_DIR}/include")
    endif()
endforeach()

set_property(TARGET Filament::backend APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES
        "Filament::utils;Filament::bluegl;Filament::bluevk;Filament::smol-v")
set_property(TARGET Filament::filabridge APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES "Filament::filaflat;Filament::utils")
set_property(TARGET Filament::filament APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES
        "Filament::backend;Filament::filabridge;Filament::filaflat;Filament::utils;Filament::zstd")
set_property(TARGET Filament::filameshio APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES
        "Filament::meshoptimizer;Filament::filament;Filament::utils")
set_property(TARGET Filament::image APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES "Filament::utils")
set_property(TARGET Filament::ktxreader APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES
        "Filament::basis_transcoder;Filament::image;Filament::filament;Filament::utils")
set_property(TARGET Filament::geometry APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES "Filament::math;Filament::utils")
set_property(TARGET Filament::stb APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES
        "Filament::filament;Filament::image;Filament::utils")
set_property(TARGET Filament::gltfio_core APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES
        "Filament::math;Filament::utils;Filament::filament;Filament::stb;Filament::ktxreader;Filament::geometry;Filament::tsl;Filament::uberzlib;Filament::dracodec;Filament::meshoptimizer")

find_package(Threads REQUIRED)
set_property(TARGET Filament::filament APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES Threads::Threads)

if(APPLE)
    find_library(FILAMENT_COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(FILAMENT_QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(FILAMENT_METAL_FRAMEWORK Metal REQUIRED)
    find_library(FILAMENT_COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    set_property(TARGET Filament::backend APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES
            "${FILAMENT_COCOA_FRAMEWORK};${FILAMENT_QUARTZCORE_FRAMEWORK};${FILAMENT_METAL_FRAMEWORK};${FILAMENT_COREVIDEO_FRAMEWORK}")
elseif(UNIX)
    set_property(TARGET Filament::backend APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS}")
elseif(WIN32)
    set_property(TARGET Filament::utils APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES shlwapi)
    set_property(TARGET Filament::bluegl APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES "opengl32;gdi32")
endif()

if(NOT TARGET Filament::Filament)
    add_library(Filament::Filament INTERFACE IMPORTED GLOBAL)
    set_target_properties(Filament::Filament PROPERTIES
        INTERFACE_LINK_LIBRARIES Filament::filament
        INTERFACE_INCLUDE_DIRECTORIES "${FILAMENT_INSTALL_DIR}/include")
endif()

# The official desktop packages also contain the matching material compiler.
# Keep its concrete package path behind an imported target so backend CMake
# files do not need to know where the release archive was installed.
foreach(FILAMENT_TOOL IN ITEMS matc cmgen filamesh resgen)
    set(FILAMENT_TOOL_EXECUTABLE
        "${FILAMENT_INSTALL_DIR}/bin/${FILAMENT_TOOL}${CMAKE_EXECUTABLE_SUFFIX}")
    if(EXISTS "${FILAMENT_TOOL_EXECUTABLE}" AND
       NOT TARGET Filament::${FILAMENT_TOOL})
        add_executable(Filament::${FILAMENT_TOOL} IMPORTED GLOBAL)
        set_target_properties(Filament::${FILAMENT_TOOL} PROPERTIES
            IMPORTED_LOCATION "${FILAMENT_TOOL_EXECUTABLE}")
    endif()
endforeach()

set(FILAMENT_FOUND TRUE)
set(FILAMENT_AVAILABLE TRUE CACHE BOOL "Filament library is available" FORCE)
set(FILAMENT_INCLUDE_DIRS "${FILAMENT_INSTALL_DIR}/include")
set(FILAMENT_LIBRARIES Filament::Filament)

function(link_filament_to_target TARGET_NAME)
    if(NOT TARGET "${TARGET_NAME}")
        message(FATAL_ERROR
            "Unknown target passed to link_filament_to_target: ${TARGET_NAME}")
    endif()
    target_link_libraries("${TARGET_NAME}" PRIVATE Filament::Filament)
endfunction()

message(STATUS "Filament is ready")
message(STATUS "  include dir: ${FILAMENT_INSTALL_DIR}/include")
message(STATUS "  library dir: ${FILAMENT_LIBRARY_DIR}")
message(STATUS "  CMake target: Filament::Filament")
if(TARGET Filament::matc)
    message(STATUS "  tools: matc, cmgen, filamesh, resgen")
endif()
message(STATUS "")
