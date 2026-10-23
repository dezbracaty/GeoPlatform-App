include_guard(GLOBAL)

# Compiler identification, rather than shell variables, selects the target ABI.
if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "Windows builds require MSVC and a matching Qt SDK.")
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

# Ninja parses /showIncludes output. Decode the compiler's console encoding into
# CMake's internal UTF-8 before the generator encodes the prefix for Ninja.
# Raw ANSI bytes can make rules.ninja generation fail in an IDE without a console.
# VSLANG alone is insufficient when only a Chinese VS pack exists.
if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja")
    if(NOT Python3_EXECUTABLE)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
    endif()
    get_filename_component(_utf8_launcher
        "${CMAKE_CURRENT_LIST_DIR}/msvc_utf8_launcher.py" ABSOLUTE)
    set(_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/GPlatformMsvcDependencies")
    file(MAKE_DIRECTORY "${_probe_dir}")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${_utf8_launcher}" --probe-header-name
        OUTPUT_VARIABLE _probe_name RESULT_VARIABLE _probe_name_result ENCODING UTF-8)
    if(NOT _probe_name_result EQUAL 0)
        message(FATAL_ERROR "Cannot select an MSVC dependency probe header")
    endif()
    string(JSON _probe_header GET "${_probe_name}" header)
    file(WRITE "${_probe_dir}/${_probe_header}" "#pragma once\n")
    file(WRITE "${_probe_dir}/probe.cpp" "#include \"${_probe_header}\"\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=VS_UNICODE_OUTPUT VSLANG=1033
            "${Python3_EXECUTABLE}" "${_utf8_launcher}"
            --detect-header "${_probe_header}" --
            "${CMAKE_CXX_COMPILER}" /nologo /utf-8 /showIncludes /c
            "${_probe_dir}/probe.cpp" "/Fo${_probe_dir}/probe.obj"
        RESULT_VARIABLE _probe_result OUTPUT_VARIABLE _probe_output
        ERROR_VARIABLE _probe_error ENCODING UTF-8)
    if(NOT _probe_result EQUAL 0)
        message(FATAL_ERROR "MSVC dependency probe failed: ${_probe_output}${_probe_error}")
    endif()
    string(JSON _output_encoding GET "${_probe_output}" encoding)
    string(JSON CMAKE_CL_SHOWINCLUDES_PREFIX GET "${_probe_output}" prefix)
    foreach(_language C CXX)
        set(CMAKE_${_language}_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
        set(_launcher "${CMAKE_COMMAND};-E;env;--unset=VS_UNICODE_OUTPUT;VSLANG=1033;--")
        list(APPEND _launcher "${Python3_EXECUTABLE}" "${_utf8_launcher}"
            --encoding "${_output_encoding}" --)
        if(CMAKE_${_language}_COMPILER_LAUNCHER)
            list(APPEND _launcher ${CMAKE_${_language}_COMPILER_LAUNCHER})
        endif()
        set(CMAKE_${_language}_COMPILER_LAUNCHER "${_launcher}")
    endforeach()
endif()
