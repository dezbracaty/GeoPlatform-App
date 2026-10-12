include_guard(GLOBAL)

set(_gplatform_libslicer_source_dir
    "${GPLATFORM_THIRDPARTY_SOURCE_DIR}/OrcaSlicer")
cmake_path(NORMAL_PATH _gplatform_libslicer_source_dir)

foreach(_required_path
        "${_gplatform_libslicer_source_dir}/CMakeLists.txt"
        "${_gplatform_libslicer_source_dir}/cmake/libslicerConfig.cmake.in"
        "${_gplatform_libslicer_source_dir}/include/libslicer/Library.hpp")
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR
            "The libslicer source tree is not initialized or is incomplete: ${_required_path}")
    endif()
endforeach()

set(_gplatform_libslicer_default_type Release)
if(WIN32)
    if(DEFINED OCCT_BUILD_TYPE)
        set(_gplatform_libslicer_occt_type "${OCCT_BUILD_TYPE}")
    else()
        set(_gplatform_libslicer_occt_type "${CMAKE_BUILD_TYPE}")
    endif()
    set(_gplatform_libslicer_default_type "${_gplatform_libslicer_occt_type}")
endif()
set(LIBSLICER_BUILD_TYPE "${_gplatform_libslicer_default_type}" CACHE STRING
    "Build type for the independently compiled libslicer package")
set_property(CACHE LIBSLICER_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
if(NOT LIBSLICER_BUILD_TYPE MATCHES
   "^(Debug|Release|RelWithDebInfo|MinSizeRel)$")
    message(FATAL_ERROR
        "Unsupported LIBSLICER_BUILD_TYPE: ${LIBSLICER_BUILD_TYPE}")
endif()

if(WIN32)
    if(NOT LIBSLICER_BUILD_TYPE STREQUAL _gplatform_libslicer_occt_type)
        message(FATAL_ERROR
            "Windows libslicer and the host OCCT must use the same configuration. "
            "Set LIBSLICER_BUILD_TYPE=${_gplatform_libslicer_occt_type} "
            "(currently ${LIBSLICER_BUILD_TYPE}).")
    endif()
    file(REAL_PATH "${OpenCASCADE_DIR}" _gplatform_libslicer_occt_dir)
    get_target_property(_gplatform_libslicer_occt_configs TKernel IMPORTED_CONFIGURATIONS)
    string(TOUPPER "${_gplatform_libslicer_occt_type}" _gplatform_libslicer_occt_config)
    if(NOT _gplatform_libslicer_occt_config IN_LIST _gplatform_libslicer_occt_configs)
        message(FATAL_ERROR "Host OCCT package does not provide ${_gplatform_libslicer_occt_type}")
    endif()
endif()

if(DEFINED CMAKE_FETCH_CACHE AND NOT CMAKE_FETCH_CACHE STREQUAL "")
    set(_gplatform_libslicer_fetch_cache "${CMAKE_FETCH_CACHE}")
elseif(DEFINED ENV{CMAKE_FETCH_CACHE} AND
       NOT "$ENV{CMAKE_FETCH_CACHE}" STREQUAL "")
    set(_gplatform_libslicer_fetch_cache "$ENV{CMAKE_FETCH_CACHE}")
else()
    message(FATAL_ERROR
        "CMAKE_FETCH_CACHE must be set before configuring libslicer")
endif()
file(TO_CMAKE_PATH "${_gplatform_libslicer_fetch_cache}"
    _gplatform_libslicer_fetch_cache)

if(CMAKE_OSX_ARCHITECTURES)
    set(_gplatform_libslicer_arch "${CMAKE_OSX_ARCHITECTURES}")
elseif(CMAKE_GENERATOR_PLATFORM)
    set(_gplatform_libslicer_arch "${CMAKE_GENERATOR_PLATFORM}")
elseif(CMAKE_SYSTEM_PROCESSOR)
    set(_gplatform_libslicer_arch "${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(_gplatform_libslicer_arch "unknown")
endif()
string(TOLOWER "${_gplatform_libslicer_arch}"
    _gplatform_libslicer_arch_slot)
string(REGEX REPLACE "[^a-z0-9_.-]+" "-"
    _gplatform_libslicer_arch_slot "${_gplatform_libslicer_arch_slot}")

# CMake build trees contain absolute source paths. Keep one stable slot per
# worktree and ABI, while source revisions reuse the same incremental tree.
string(SHA256 _gplatform_libslicer_source_hash
    "${_gplatform_libslicer_source_dir}")
string(SUBSTRING "${_gplatform_libslicer_source_hash}" 0 16
    _gplatform_libslicer_source_slot)
set(_gplatform_libslicer_abi_identity
    "package_layout=2\n"
    "build_type=${LIBSLICER_BUILD_TYPE}\n"
    "arch=${_gplatform_libslicer_arch}\n"
    "generator=${CMAKE_GENERATOR}\n"
    "generator_platform=${CMAKE_GENERATOR_PLATFORM}\n"
    "generator_toolset=${CMAKE_GENERATOR_TOOLSET}\n"
    "c_compiler=${CMAKE_C_COMPILER}\n"
    "c_compiler_id=${CMAKE_C_COMPILER_ID}\n"
    "c_compiler_version=${CMAKE_C_COMPILER_VERSION}\n"
    "cxx_compiler=${CMAKE_CXX_COMPILER}\n"
    "cxx_compiler_id=${CMAKE_CXX_COMPILER_ID}\n"
    "cxx_compiler_version=${CMAKE_CXX_COMPILER_VERSION}\n"
    "deployment_target=${CMAKE_OSX_DEPLOYMENT_TARGET}\n"
    "sysroot=${CMAKE_OSX_SYSROOT}\n")
if(WIN32)
    list(APPEND _gplatform_libslicer_abi_identity
        "occt_package=${_gplatform_libslicer_occt_dir}\n"
        "occt_version=${GPLATFORM_OCCT_VERSION}\n"
        "occt_configuration=${_gplatform_libslicer_occt_config}\n")
endif()
string(SHA256 _gplatform_libslicer_abi_hash
    "${_gplatform_libslicer_abi_identity}")
string(SUBSTRING "${_gplatform_libslicer_abi_hash}" 0 16
    _gplatform_libslicer_abi_slot)

set(GPLATFORM_LIBSLICER_CACHE_ROOT
    "${_gplatform_libslicer_fetch_cache}/libslicer/${_gplatform_libslicer_source_slot}/${_gplatform_libslicer_arch_slot}/${_gplatform_libslicer_abi_slot}"
    CACHE PATH "Stable libslicer build cache root" FORCE)
set(_gplatform_libslicer_build_dir
    "${GPLATFORM_LIBSLICER_CACHE_ROOT}/build-${LIBSLICER_BUILD_TYPE}")
set(GPLATFORM_LIBSLICER_INSTALL_DIR
    "${GPLATFORM_LIBSLICER_CACHE_ROOT}/install-${LIBSLICER_BUILD_TYPE}"
    CACHE PATH "Exact libslicer installation prefix" FORCE)
set(GPLATFORM_LIBSLICER_PACKAGE_DIR
    "${GPLATFORM_LIBSLICER_INSTALL_DIR}/lib/cmake/libslicer"
    CACHE PATH "Exact libslicer CMake package directory" FORCE)

if(COMMAND gplatform_check_cache_contract)
    gplatform_check_cache_contract("${_gplatform_libslicer_build_dir}")
endif()
file(MAKE_DIRECTORY "${GPLATFORM_LIBSLICER_CACHE_ROOT}")
file(LOCK "${GPLATFORM_LIBSLICER_CACHE_ROOT}/build.lock"
    GUARD FILE TIMEOUT 3600
    RESULT_VARIABLE _gplatform_libslicer_lock_result)
if(NOT "${_gplatform_libslicer_lock_result}" STREQUAL "0")
    message(FATAL_ERROR
        "Unable to lock libslicer cache: ${_gplatform_libslicer_lock_result}")
endif()

# Register child inputs with the parent generator. A regular App build then
# reconfigures and incrementally rebuilds the standalone package after edits.
file(GLOB_RECURSE _gplatform_libslicer_inputs
    CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
    "${_gplatform_libslicer_source_dir}/CMakeLists.txt"
    "${_gplatform_libslicer_source_dir}/version.inc"
    "${_gplatform_libslicer_source_dir}/cmake/*.cmake"
    "${_gplatform_libslicer_source_dir}/cmake/*.in"
    "${_gplatform_libslicer_source_dir}/deps/*.cmake"
    "${_gplatform_libslicer_source_dir}/deps/*/*.cmake"
    "${_gplatform_libslicer_source_dir}/deps_src/*/CMakeLists.txt"
    "${_gplatform_libslicer_source_dir}/include/*.h"
    "${_gplatform_libslicer_source_dir}/include/*.hpp"
    "${_gplatform_libslicer_source_dir}/resources/profiles/*.json"
    "${_gplatform_libslicer_source_dir}/src/*.c"
    "${_gplatform_libslicer_source_dir}/src/*.cc"
    "${_gplatform_libslicer_source_dir}/src/*.cpp"
    "${_gplatform_libslicer_source_dir}/src/*.h"
    "${_gplatform_libslicer_source_dir}/src/*.hpp")
set_property(DIRECTORY APPEND PROPERTY
    CMAKE_CONFIGURE_DEPENDS ${_gplatform_libslicer_inputs})

set(_gplatform_libslicer_configure_args
    -S "${_gplatform_libslicer_source_dir}"
    -B "${_gplatform_libslicer_build_dir}"
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_BUILD_TYPE=${LIBSLICER_BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=${GPLATFORM_LIBSLICER_INSTALL_DIR}"
    "-DCMAKE_INSTALL_LIBDIR=lib"
    "-DCMAKE_FETCH_CACHE=${_gplatform_libslicer_fetch_cache}"
    "-DLIBSLICER_BUILD_TESTS=OFF"
    "-DSLIC3R_PCH=ON")

if(WIN32)
    # Both DLLs are loaded in the same process and must use one OCCT ABI.
    list(APPEND _gplatform_libslicer_configure_args
        "-DLIBSLICER_EXTERNAL_OCCT_DIR=${OpenCASCADE_DIR}")
endif()
if(PERL_EXECUTABLE AND NOT PERL_EXECUTABLE MATCHES "-NOTFOUND$")
    # OpenSSL's dependency script has its own native-Perl selector.
    list(APPEND _gplatform_libslicer_configure_args "-DOPENSSL_PERL=${PERL_EXECUTABLE}")
else()
    # Clear previous host-provided discovery results in the persistent child
    # cache. OpenSSL can discover Perl itself when a source build needs it.
    list(APPEND _gplatform_libslicer_configure_args "-DOPENSSL_PERL=" "-DPERL_EXECUTABLE=")
endif()

foreach(_cache_name
        GIT_EXECUTABLE
        PERL_EXECUTABLE
        Python3_EXECUTABLE
        CMAKE_C_COMPILER
        CMAKE_CXX_COMPILER
        CMAKE_MAKE_PROGRAM
        CMAKE_TOOLCHAIN_FILE
        CMAKE_OSX_ARCHITECTURES
        CMAKE_OSX_DEPLOYMENT_TARGET
        CMAKE_OSX_SYSROOT
        CMAKE_GENERATOR_PLATFORM
        CMAKE_GENERATOR_TOOLSET)
    if(DEFINED ${_cache_name} AND NOT "${${_cache_name}}" STREQUAL ""
       AND NOT "${${_cache_name}}" MATCHES "-NOTFOUND$")
        list(APPEND _gplatform_libslicer_configure_args
            "-D${_cache_name}=${${_cache_name}}")
    endif()
endforeach()

message(STATUS
    "Configuring standalone libslicer ${LIBSLICER_BUILD_TYPE}: ${_gplatform_libslicer_build_dir}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_gplatform_libslicer_configure_args}
    RESULT_VARIABLE _gplatform_libslicer_configure_result
    COMMAND_ECHO STDOUT)
if(NOT "${_gplatform_libslicer_configure_result}" STREQUAL "0")
    message(FATAL_ERROR
        "libslicer configure failed: ${_gplatform_libslicer_configure_result}")
endif()

message(STATUS "Building and installing standalone libslicer package")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_gplatform_libslicer_build_dir}"
        --config "${LIBSLICER_BUILD_TYPE}"
        --target install

    RESULT_VARIABLE _gplatform_libslicer_build_result
    COMMAND_ECHO STDOUT)
if(NOT "${_gplatform_libslicer_build_result}" STREQUAL "0")
    message(FATAL_ERROR
        "libslicer install failed: ${_gplatform_libslicer_build_result}")
endif()

if(NOT EXISTS "${GPLATFORM_LIBSLICER_PACKAGE_DIR}/libslicerConfig.cmake")
    message(FATAL_ERROR
        "libslicer package config is missing: ${GPLATFORM_LIBSLICER_PACKAGE_DIR}")
endif()

message(STATUS "libslicer source: ${_gplatform_libslicer_source_dir}")
message(STATUS "libslicer stable cache: ${GPLATFORM_LIBSLICER_CACHE_ROOT}")
message(STATUS "libslicer package: ${GPLATFORM_LIBSLICER_PACKAGE_DIR}")
