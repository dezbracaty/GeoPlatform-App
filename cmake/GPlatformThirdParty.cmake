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

foreach(fork SmoothUI SmoothUIDock OrcaSlicer SmoothUIDock/KDDockWidgets)
    if(NOT EXISTS "${GPLATFORM_THIRDPARTY_SOURCE_DIR}/${fork}/CMakeLists.txt")
        message(FATAL_ERROR "Missing App/ThirdParty/${fork}. Run git submodule update --init --recursive before configure, or use the complete exported App package.")
    endif()
endforeach()

include(fetch_spdlog)
include(fetch_eigen3)
include(fetch_cereal)
include(fetch_rttr)
set(GPLATFORM_ENABLE_OCCT ON CACHE BOOL "Enable OpenCASCADE (OCCT) dependency integration" FORCE)
include(fetch_occt)
if(ENABLE_BREAKPAD)
    include(fetch_breakpad)
endif()
set(VTK_VERSION "9.5.0" CACHE STRING "VTK version to use")
include(fetch_vtk)
include(fetch_renderer_environment)
if(GPLATFORM_ENABLE_FILAMENT)
    set(FILAMENT_VERSION "1.75.0" CACHE STRING "Filament version to use")
    include(fetch_filament)
endif()
set(EMBREE_VERSION "4.4.0" CACHE STRING "Embree version to use")
include(fetch_embree)

# SmoothUIImpl links this private Qt module; load it before adding the fork.
find_package(Qt6 REQUIRED COMPONENTS QmlPrivate)

add_subdirectory("${GPLATFORM_THIRDPARTY_SOURCE_DIR}/SmoothUI"
    "${CMAKE_BINARY_DIR}/third_party/SmoothUI")
add_subdirectory("${GPLATFORM_THIRDPARTY_SOURCE_DIR}/SmoothUIDock"
    "${CMAKE_BINARY_DIR}/third_party/SmoothUIDock")
if(NOT TARGET miniz)
    add_subdirectory("${GPLATFORM_THIRDPARTY_SOURCE_DIR}/OrcaSlicer/deps_src/miniz"
        "${CMAKE_BINARY_DIR}/third_party/miniz" EXCLUDE_FROM_ALL)
endif()
if(GPLATFORM_ENABLE_LIBSLICER)
    include(fetch_libslicer)
    set(libslicer_DIR "${GPLATFORM_LIBSLICER_PACKAGE_DIR}" CACHE PATH "Exact libslicer package directory used by GPlatform" FORCE)
    find_package(libslicer CONFIG REQUIRED PATHS "${GPLATFORM_LIBSLICER_PACKAGE_DIR}" NO_DEFAULT_PATH)
endif()
set(GPLATFORM_I18N_EXTRA_SOURCES
    "${GPLATFORM_THIRDPARTY_SOURCE_DIR}/SmoothUI/Controls/FileBrowserDialog.qml"
    "${GPLATFORM_THIRDPARTY_SOURCE_DIR}/SmoothUI/Controls/FilePicker.qml"
    "${GPLATFORM_THIRDPARTY_SOURCE_DIR}/SmoothUI/QuickActionMenu.qml")
