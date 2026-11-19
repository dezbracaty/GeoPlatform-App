include_guard(GLOBAL)

# Both producers and standalone consumers use this App-owned provider.
include("${CMAKE_CURRENT_LIST_DIR}/GPlatformCacheContract.cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/thirdparty")
get_filename_component(GPLATFORM_THIRDPARTY_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty" ABSOLUTE)

if(NOT Qt6_FOUND)
    message(FATAL_ERROR "Find the required local Qt6 before configuring third-party dependencies.")
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # Preserve the former ThirdParty default for a root configure without a preset.
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
option(SMOOTHUI_BUILD_STATIC_LIB "Build SmoothUI static library." ON)
option(SMOOTHUI_UNIVERSAL_BUILD_MACOS "Universal build SmoothUI for macOS." ON)
if(NOT APPLE)
    set(SMOOTHUI_UNIVERSAL_BUILD_MACOS OFF)
endif()
option(GPLATFORM_ENABLE_LIBSLICER "Build and link the libslicer core library" ON)
option(GPLATFORM_ENABLE_FILAMENT "Enable the Filament renderer dependency" ON)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    option(ENABLE_BREAKPAD "Enable Google Breakpad crash reporting" ON)
else()
    option(ENABLE_BREAKPAD "Enable Google Breakpad crash reporting" OFF)
endif()
set(FETCHCONTENT_BASE_DIR "${CMAKE_FETCH_CACHE}" CACHE PATH "Base directory for FetchContent")
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Disable automatic updates for FetchContent")

foreach(fork OrcaSlicer)
    if(NOT EXISTS "${GPLATFORM_THIRDPARTY_SOURCE_DIR}/${fork}/CMakeLists.txt")
        message(FATAL_ERROR "Missing App/ThirdParty/${fork}. Run git submodule update --init --recursive before configure, or use the complete exported App package.")
    endif()
endforeach()

include(fetch_spdlog)
include(fetch_qhull)
include(fetch_cereal)
include(fetch_rttr)
set(GPLATFORM_ENABLE_OCCT ON CACHE BOOL "Enable OpenCASCADE (OCCT) dependency integration" FORCE)
include(fetch_occt)
if(ENABLE_BREAKPAD)
    include(fetch_breakpad)
endif()
include(fetch_renderer_environment)
if(GPLATFORM_ENABLE_FILAMENT)
    set(FILAMENT_VERSION "1.75.0" CACHE STRING "Filament version to use")
    include(fetch_filament)
endif()
set(EMBREE_VERSION "4.4.0" CACHE STRING "Embree version to use")
include(fetch_embree)

function(_gplatform_add_embedded_libslicer source_dir binary_dir install_dir)
    # Match the resource fallback compiled by Orca to our staged installation.
    set(CMAKE_INSTALL_PREFIX "${install_dir}")
    add_subdirectory("${source_dir}" "${binary_dir}" EXCLUDE_FROM_ALL)
endfunction()

if(GPLATFORM_ENABLE_LIBSLICER)
    include(GNUInstallDirs)
    set(GPLATFORM_LIBSLICER_SOURCE_DIR "${GPLATFORM_THIRDPARTY_SOURCE_DIR}/OrcaSlicer"
        CACHE INTERNAL "Orca source used by this build")
    set(_libslicer_binary_dir "${CMAKE_BINARY_DIR}/libslicer")
    set(_libslicer_install_dir "${_libslicer_binary_dir}/install")
    set(GPLATFORM_LIBSLICER_INSTALL_DIR "${_libslicer_install_dir}"
        CACHE INTERNAL "Staged libslicer package for this build")
    set(LIBSLICER_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    set(LIBSLICER_BUILD_TESTS OFF)
    set(LIBSLICER_NATIVE_REGRESSION_CMAKE "${GPLATFORM_ORCA_NATIVE_REGRESSION_CMAKE}"
        CACHE INTERNAL "Opt-in Orca probe targets for this build")
    # Orca and App share the OCCT targets selected by the host.
    set(LIBSLICER_EXTERNAL_OCCT_DIR "${OpenCASCADE_DIR}"
        CACHE INTERNAL "OCCT package selected by the App or host")
    _gplatform_add_embedded_libslicer(
        "${GPLATFORM_LIBSLICER_SOURCE_DIR}" "${_libslicer_binary_dir}" "${_libslicer_install_dir}")
    set(LIBSLICER_RESOURCE_DIR
        "${GPLATFORM_LIBSLICER_INSTALL_DIR}/${LIBSLICER_INSTALL_RESOURCEDIR}")
    set(GPLATFORM_LIBSLICER_RUNTIME_DIR
        "${_libslicer_install_dir}/${CMAKE_INSTALL_BINDIR}"
        CACHE INTERNAL "Staged Windows runtime dependencies for libslicer")

    # Stage Orca's curated resources and private runtime dependencies with its
    # existing install rules. Linking uses the in-tree target directly.
    file(GLOB_RECURSE _libslicer_resources CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
        "${GPLATFORM_LIBSLICER_SOURCE_DIR}/resources/*")
    set(_libslicer_resource_list "${_libslicer_binary_dir}/resources.txt")
    file(GENERATE OUTPUT "${_libslicer_resource_list}" CONTENT "${_libslicer_resources}")
    set(GPLATFORM_LIBSLICER_INSTALL_STAMP "${_libslicer_install_dir}/.installed")
    add_custom_command(OUTPUT "${GPLATFORM_LIBSLICER_INSTALL_STAMP}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_libslicer_install_dir}"
        COMMAND "${CMAKE_COMMAND}" --install "${_libslicer_binary_dir}"
            --prefix "${_libslicer_install_dir}" --config "$<CONFIG>"
        COMMAND "${CMAKE_COMMAND}" -E touch "${GPLATFORM_LIBSLICER_INSTALL_STAMP}"
        DEPENDS libslicer_api "${_libslicer_resource_list}" ${_libslicer_resources}
            "${_libslicer_binary_dir}/cmake_install.cmake"
        COMMENT "Staging libslicer runtime and resources"
        VERBATIM)
    add_custom_target(gplatform_libslicer_install
        DEPENDS "${GPLATFORM_LIBSLICER_INSTALL_STAMP}")
elseif(NOT TARGET miniz)
    add_subdirectory("${GPLATFORM_THIRDPARTY_SOURCE_DIR}/OrcaSlicer/deps_src/miniz"
        "${CMAKE_BINARY_DIR}/third_party/miniz" EXCLUDE_FROM_ALL)
endif()

# Orca resolves Eigen in its own directory before the App defines Eigen3::Eigen.
include(fetch_eigen3)
# Run VTK's FindZLIB after Orca has prepared the pinned compression dependencies.
set(VTK_VERSION "9.5.0" CACHE STRING "VTK version to use")
include(fetch_vtk)
