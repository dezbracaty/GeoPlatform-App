cmake_minimum_required(VERSION 3.16)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

# Support both the native Windows and Unix installation layouts, including
# caches produced before the platform-specific package directory was handled.
macro(gplatform_load_occt)
    unset(OpenCASCADE_DIR CACHE)
    unset(OpenCASCADE_DIR)
    find_package(OpenCASCADE REQUIRED CONFIG GLOBAL
        PATHS "${OCCT_INSTALL_DIR}/cmake"
              "${OCCT_INSTALL_DIR}/lib/cmake/opencascade"
        NO_DEFAULT_PATH)
    foreach(_occt_target IN LISTS OpenCASCADE_LIBRARIES)
        if(TARGET "${_occt_target}")
            set_property(TARGET "${_occt_target}" APPEND PROPERTY
                INTERFACE_INCLUDE_DIRECTORIES "${OpenCASCADE_INCLUDE_DIR}")
        endif()
    endforeach()
endmacro()

# OCCT version config
if(NOT DEFINED GPLATFORM_OCCT_VERSION)
    set(GPLATFORM_OCCT_VERSION "7.8.1" CACHE STRING "OpenCASCADE version to build")
endif()

string(REPLACE "." "_" OCCT_TAG_VERSION "${GPLATFORM_OCCT_VERSION}")

# OCCT 版本与 SHA256 校验表
set(OCCT_KNOWN_SHA256_7.8.1 "7321af48c34dc253bf8aae3f0430e8cb10976961d534d8509e72516978aa82f5")

if(NOT DEFINED OCCT_KNOWN_SHA256_${GPLATFORM_OCCT_VERSION})
    message(FATAL_ERROR
        "❌ OCCT ${GPLATFORM_OCCT_VERSION} 未登记 SHA256，请在 fetch_occt.cmake 顶部补充 OCCT_KNOWN_SHA256_${GPLATFORM_OCCT_VERSION}")
endif()
set(OCCT_EXPECTED_SHA256 "${OCCT_KNOWN_SHA256_${GPLATFORM_OCCT_VERSION}}")

# Build type logic (same pattern as fetch_vtk)
if(DEFINED OCCT_BUILD_TYPE)
    set(OCCT_ACTUAL_BUILD_TYPE "${OCCT_BUILD_TYPE}")
elseif(CMAKE_BUILD_TYPE)
    if(CMAKE_GENERATOR MATCHES "Visual Studio" AND CMAKE_BUILD_TYPE MATCHES ";")
        set(OCCT_ACTUAL_BUILD_TYPE "Debug")
    else()
        set(OCCT_ACTUAL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    endif()
else()
    set(OCCT_ACTUAL_BUILD_TYPE "Debug")
endif()

gplatform_resolve_fetch_cache(
    OCCT "occt" "${GPLATFORM_OCCT_VERSION}" "${OCCT_ACTUAL_BUILD_TYPE}")
set(OCCT_ZIP_FILE "${OCCT_CACHE_DIR}/OCCT-V${OCCT_TAG_VERSION}.tar.gz")

message(STATUS "")
message(STATUS "OCCT cache manager")
message(STATUS "  version: ${GPLATFORM_OCCT_VERSION}")
message(STATUS "  build type: ${OCCT_ACTUAL_BUILD_TYPE}")
message(STATUS "  cache root: ${FETCH_CACHE_DIR}")
message(STATUS "  cache dir: ${OCCT_CACHE_DIR}")
message(STATUS "  target arch: ${FETCH_CACHE_ARCH}")
message(STATUS "  source dir: ${OCCT_SOURCE_DIR}")
message(STATUS "  build dir: ${OCCT_BUILD_DIR}")
message(STATUS "  install dir: ${OCCT_INSTALL_DIR}")
message(STATUS "")

set(HAS_INSTALL FALSE)
set(HAS_BUILD FALSE)
set(HAS_SOURCE FALSE)
set(HAS_ZIP FALSE)
set(BUILD_COMPLETE FALSE)

if(EXISTS "${OCCT_INSTALL_DIR}/lib/cmake/opencascade/OpenCASCADEConfig.cmake" OR
   EXISTS "${OCCT_INSTALL_DIR}/cmake/OpenCASCADEConfig.cmake")
    set(HAS_INSTALL TRUE)
    message(STATUS "OCCT: install cache found: ${OCCT_INSTALL_DIR}")
endif()

if(EXISTS "${OCCT_BUILD_DIR}/OpenCASCADEConfig.cmake" OR
   EXISTS "${OCCT_BUILD_DIR}/adm/cmake/OpenCASCADEConfig.cmake" OR
   EXISTS "${OCCT_BUILD_DIR}/lib/cmake/opencascade/OpenCASCADEConfig.cmake")
    set(HAS_BUILD TRUE)
    set(BUILD_COMPLETE TRUE)
    message(STATUS "OCCT: complete build cache found: ${OCCT_BUILD_DIR}")
elseif(EXISTS "${OCCT_BUILD_DIR}/CMakeCache.txt")
    set(HAS_BUILD TRUE)
    set(BUILD_COMPLETE FALSE)
    message(STATUS "OCCT: partial build cache found: ${OCCT_BUILD_DIR}")
endif()

if(EXISTS "${OCCT_SOURCE_DIR}/CMakeLists.txt")
    set(HAS_SOURCE TRUE)
    message(STATUS "OCCT: source cache found: ${OCCT_SOURCE_DIR}")
endif()

if(EXISTS "${OCCT_ZIP_FILE}")
    file(SHA256 "${OCCT_ZIP_FILE}" OCCT_ACTUAL_SHA256)
    if(OCCT_ACTUAL_SHA256 STREQUAL "${OCCT_EXPECTED_SHA256}")
        set(HAS_ZIP TRUE)
        message(STATUS "OCCT: source archive found: ${OCCT_ZIP_FILE} (SHA256 verified)")
    else()
        message(WARNING "OCCT: cached archive SHA256 mismatch, removing for re-download\n"
            "   expected: ${OCCT_EXPECTED_SHA256}\n"
            "   actual:   ${OCCT_ACTUAL_SHA256}")
        file(REMOVE "${OCCT_ZIP_FILE}")
    endif()
endif()

set(OCCT_STATUS "NONE")
set(OCCT_FOUND FALSE)

# install > build > source > zip > none
if(HAS_INSTALL)
    set(OCCT_STATUS "INSTALLED")
    set(OCCT_FOUND TRUE)
elseif(HAS_BUILD AND NOT HAS_INSTALL)
    if(BUILD_COMPLETE)
        set(OCCT_STATUS "BUILT_COMPLETE")
        set(OCCT_FOUND TRUE)
    else()
        set(OCCT_STATUS "BUILT_NOT_INSTALLED")
    endif()
elseif(HAS_SOURCE AND NOT HAS_BUILD)
    set(OCCT_STATUS "SOURCE_ONLY")
elseif(HAS_ZIP AND NOT HAS_SOURCE)
    set(OCCT_STATUS "ZIP_ONLY")
else()
    set(OCCT_STATUS "NONE")
endif()

message(STATUS "OCCT status: Install=${HAS_INSTALL}, Build=${HAS_BUILD}, Source=${HAS_SOURCE}, Zip=${HAS_ZIP}")
message(STATUS "OCCT strategy: ${OCCT_STATUS}")

if(OCCT_STATUS STREQUAL "INSTALLED")
    gplatform_load_occt()

elseif(OCCT_STATUS STREQUAL "BUILT_COMPLETE")
    message(STATUS "OCCT: installing from build cache...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${OCCT_BUILD_DIR} --target install --config ${OCCT_ACTUAL_BUILD_TYPE}
        RESULT_VARIABLE install_result
        WORKING_DIRECTORY ${OCCT_BUILD_DIR}
    )
    if(install_result EQUAL 0)
        gplatform_load_occt()
        set(OCCT_FOUND TRUE)
    else()
        message(FATAL_ERROR "OCCT install failed")
    endif()

elseif(OCCT_STATUS STREQUAL "BUILT_NOT_INSTALLED")
    message(STATUS "OCCT: build cache exists but incomplete; trying install first...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${OCCT_BUILD_DIR} --target install --config ${OCCT_ACTUAL_BUILD_TYPE}
        RESULT_VARIABLE install_result
        WORKING_DIRECTORY ${OCCT_BUILD_DIR}
    )
    if(install_result EQUAL 0)
        gplatform_load_occt()
        set(OCCT_FOUND TRUE)
    else()
        message(WARNING "OCCT install from partial build failed; rebuilding from source cache...")
        file(REMOVE_RECURSE "${OCCT_BUILD_DIR}")
        set(OCCT_STATUS "SOURCE_ONLY")
        include(${CMAKE_CURRENT_LIST_FILE})
    endif()

elseif(OCCT_STATUS STREQUAL "SOURCE_ONLY")
    message(STATUS "OCCT: configuring and building from source cache...")

    set(OCCT_GENERATOR "")
    set(OCCT_GENERATOR_PLATFORM "")
    set(OCCT_MAKE_PROGRAM "")

    if(CMAKE_GENERATOR)
        set(OCCT_GENERATOR "${CMAKE_GENERATOR}")
        if(CMAKE_GENERATOR_PLATFORM)
            set(OCCT_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
        endif()
    else()
        find_program(NINJA_EXECUTABLE ninja)
        if(NINJA_EXECUTABLE)
            set(OCCT_GENERATOR "Ninja")
            set(OCCT_MAKE_PROGRAM ${NINJA_EXECUTABLE})
        else()
            set(OCCT_GENERATOR "Unix Makefiles")
            if(CMAKE_MAKE_PROGRAM)
                set(OCCT_MAKE_PROGRAM "${CMAKE_MAKE_PROGRAM}")
            endif()
        endif()
    endif()

    set(GENERATOR_ARGS -G "${OCCT_GENERATOR}")
    if(OCCT_GENERATOR_PLATFORM)
        list(APPEND GENERATOR_ARGS -A ${OCCT_GENERATOR_PLATFORM})
    endif()
    if(OCCT_MAKE_PROGRAM)
        list(APPEND GENERATOR_ARGS -DCMAKE_MAKE_PROGRAM=${OCCT_MAKE_PROGRAM})
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND}
            ${GENERATOR_ARGS}
            -S ${OCCT_SOURCE_DIR}
            -B ${OCCT_BUILD_DIR}
            ${FETCH_CACHE_FORWARD_ARGS}
            -DCMAKE_BUILD_TYPE=${OCCT_ACTUAL_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${OCCT_INSTALL_DIR}
            -DBUILD_RELEASE_DISABLE_EXCEPTIONS=OFF
            # GPlatform uses the modeling kernel, not OCCT's Tcl/Tk Draw harness.
            -DBUILD_MODULE_Draw=OFF
            -DUSE_TK=OFF
            -DUSE_TBB=OFF
            -DUSE_FREEIMAGE=OFF
            -DUSE_FREETYPE=OFF
        RESULT_VARIABLE config_result
        OUTPUT_VARIABLE config_output
        ERROR_VARIABLE config_error
    )

    if(config_result EQUAL 0)
        message(STATUS "OCCT: configure completed")


        execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${OCCT_BUILD_DIR} --config ${OCCT_ACTUAL_BUILD_TYPE}
            RESULT_VARIABLE build_result
            OUTPUT_VARIABLE build_output
            ERROR_VARIABLE build_error
            WORKING_DIRECTORY ${OCCT_BUILD_DIR}
        )

        if(build_result EQUAL 0)
            message(STATUS "OCCT: build completed")

            execute_process(
                COMMAND ${CMAKE_COMMAND} --build ${OCCT_BUILD_DIR} --target install --config ${OCCT_ACTUAL_BUILD_TYPE}
                RESULT_VARIABLE install_result
                OUTPUT_VARIABLE install_output
                ERROR_VARIABLE install_error
                WORKING_DIRECTORY ${OCCT_BUILD_DIR}
            )

            if(install_result EQUAL 0)
                message(STATUS "OCCT: install completed")
                gplatform_load_occt()
                set(OCCT_FOUND TRUE)
            else()
                message(STATUS "OCCT install output:")
                message(STATUS "${install_output}")
                message(STATUS "OCCT install error:")
                message(STATUS "${install_error}")
                message(FATAL_ERROR "OCCT install failed")
            endif()
        else()
            message(STATUS "OCCT build output:")
            message(STATUS "${build_output}")
            message(STATUS "OCCT build error:")
            message(STATUS "${build_error}")
            message(FATAL_ERROR "OCCT build failed")
        endif()
    else()
        message(STATUS "OCCT configure output:")
        message(STATUS "${config_output}")
        message(STATUS "OCCT configure error:")
        message(STATUS "${config_error}")
        message(FATAL_ERROR "OCCT configure failed")
    endif()

elseif(OCCT_STATUS STREQUAL "ZIP_ONLY")
    message(STATUS "OCCT: extracting source archive...")

    if(EXISTS "${OCCT_SOURCE_DIR}")
        file(REMOVE_RECURSE "${OCCT_SOURCE_DIR}")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${OCCT_ZIP_FILE}
        WORKING_DIRECTORY ${OCCT_CACHE_DIR}
        RESULT_VARIABLE extract_result
        ERROR_VARIABLE extract_error
    )

    if(extract_result EQUAL 0)
        file(GLOB OCCT_EXTRACTED_DIRS "${OCCT_CACHE_DIR}/OCCT-*")
        list(LENGTH OCCT_EXTRACTED_DIRS OCCT_EXTRACTED_DIRS_COUNT)
        if(OCCT_EXTRACTED_DIRS_COUNT GREATER 0)
            list(GET OCCT_EXTRACTED_DIRS 0 OCCT_EXTRACTED_DIR)
            if(EXISTS "${OCCT_EXTRACTED_DIR}")
                file(RENAME "${OCCT_EXTRACTED_DIR}" "${OCCT_SOURCE_DIR}")
                set(OCCT_STATUS "SOURCE_ONLY")
                include(${CMAKE_CURRENT_LIST_FILE})
            else()
                message(FATAL_ERROR "OCCT extract failed: extracted directory not found")
            endif()
        else()
            message(FATAL_ERROR "OCCT extract failed: no OCCT-* directory found")
        endif()
    else()
        message(WARNING "OCCT extract failed: ${extract_error}")
        file(REMOVE "${OCCT_ZIP_FILE}")
        set(OCCT_STATUS "NONE")
        include(${CMAKE_CURRENT_LIST_FILE})
    endif()

else()
    # NONE
    message(STATUS "OCCT: downloading source archive...")

    set(OCCT_DOWNLOAD_URL "https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V${OCCT_TAG_VERSION}.tar.gz")
    gplatform_download(
        ${OCCT_DOWNLOAD_URL}
        ${OCCT_ZIP_FILE}
        EXPECTED_HASH SHA256=${OCCT_EXPECTED_SHA256}
        STATUS download_status
        LOG download_log
        SHOW_PROGRESS
    )

    list(GET download_status 0 status_code)
    list(GET download_status 1 status_msg)

    if(NOT status_code EQUAL 0)
        message(FATAL_ERROR "OCCT download failed: ${status_msg}\n${download_log}")
    endif()
    message(STATUS "OCCT: download complete (SHA256 verified)")

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${OCCT_ZIP_FILE}
        WORKING_DIRECTORY ${OCCT_CACHE_DIR}
        RESULT_VARIABLE extract_result
    )

    if(extract_result EQUAL 0)
        message(STATUS "OCCT: download and extract completed")
        set(OCCT_STATUS "SOURCE_ONLY")
        include(${CMAKE_CURRENT_LIST_FILE})
    else()
        message(FATAL_ERROR "OCCT extract failed")
    endif()
endif()

if(OpenCASCADE_FOUND)
    set(OCCT_FOUND TRUE CACHE INTERNAL "OCCT availability flag")
    message(STATUS "OCCT ready: ${OpenCASCADE_DIR}")
else()
    set(OCCT_FOUND FALSE CACHE INTERNAL "OCCT availability flag")
    message(WARNING "OCCT is not available")
endif()
