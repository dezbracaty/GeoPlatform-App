include_guard(GLOBAL)

# Compiler identification, rather than shell variables, selects the target ABI.
if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "Windows builds require MSVC and a matching Qt SDK.")
endif()

# Ninja parses /showIncludes by its exact prefix. Keep the compiler's language
# and the generated Ninja rule consistent, including on localized Windows.
# Existing user launchers remain inside this environment wrapper.
if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja")
    foreach(_gplatform_language IN ITEMS C CXX)
        list(PREPEND CMAKE_${_gplatform_language}_COMPILER_LAUNCHER
            "${CMAKE_COMMAND}" -E env VSLANG=1033 --)
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
