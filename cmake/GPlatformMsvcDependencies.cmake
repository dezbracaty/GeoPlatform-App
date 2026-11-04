include_guard(GLOBAL)

# Compiler identification, rather than shell variables, selects the target ABI.
if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "Windows builds require MSVC and a matching Qt SDK.")
endif()

# Ninja needs a UTF-8 dependency prefix even when MSVC uses a local code page.
# Normalize only dependency lines; keep compiler arguments and exit status intact.
if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    foreach(_gplatform_language IN ITEMS C CXX)
        list(PREPEND CMAKE_${_gplatform_language}_COMPILER_LAUNCHER
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/msvc_includes.py"
            --prefix "${CMAKE_${_gplatform_language}_CL_SHOWINCLUDES_PREFIX}" --)
    endforeach()
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file: ")
endif()
set(_compiler_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(MSVC)
    set(_compiler_arch "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(_compiler_arch "${CMAKE_OSX_ARCHITECTURES}")
endif()
if(GPLATFORM_TARGET_ARCH)
    string(TOLOWER "${_compiler_arch}" _actual_arch)
    string(TOLOWER "${GPLATFORM_TARGET_ARCH}" _requested_arch)
    string(REGEX REPLACE "^(amd64|x86_64)$" "x64" _actual_arch "${_actual_arch}")
    string(REGEX REPLACE "^(amd64|x86_64)$" "x64" _requested_arch "${_requested_arch}")
    if(NOT _actual_arch STREQUAL _requested_arch)
        message(FATAL_ERROR "GPLATFORM_TARGET_ARCH=${GPLATFORM_TARGET_ARCH} does not match compiler target ${_compiler_arch}.")
    endif()
endif()
