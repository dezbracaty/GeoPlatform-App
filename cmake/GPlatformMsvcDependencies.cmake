include_guard(GLOBAL)

# Ninja compares /showIncludes prefixes as raw bytes. Preserve the compiler's
# actual output; decoding AUTO can corrupt UTF-8 from Chinese MSVC under an IDE.
if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja")
    set(_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/GPlatformMsvcDependencies")
    file(MAKE_DIRECTORY "${_probe_dir}")
    file(WRITE "${_probe_dir}/gplatform_dependency_probe.h" "#pragma once\n")
    file(WRITE "${_probe_dir}/probe.cpp" "#include \"gplatform_dependency_probe.h\"\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=VS_UNICODE_OUTPUT VSLANG=1033
            "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c
            "${_probe_dir}/probe.cpp" "/Fo${_probe_dir}/probe.obj"
        RESULT_VARIABLE _probe_result OUTPUT_VARIABLE _probe_output
        ERROR_VARIABLE _probe_error ENCODING NONE)
    if(NOT _probe_result EQUAL 0)
        message(FATAL_ERROR "MSVC dependency probe failed: ${_probe_output}${_probe_error}")
    endif()
    # Capture everything before the absolute included-header path, without
    # assuming an installed language pack or the wording of the diagnostics.
    string(REGEX MATCH "(^|\n)([^\r\n]*: +)[A-Za-z]:[^\r\n]*gplatform_dependency_probe\\.h"
        _probe_match "${_probe_output}")
    if(NOT _probe_match)
        message(FATAL_ERROR "Cannot detect MSVC /showIncludes prefix: ${_probe_output}")
    endif()
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
    foreach(_language C CXX)
        set(CMAKE_${_language}_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
        # The explicit command delimiter also refreshes dependency records
        # produced by the previous launcher with an incorrectly decoded prefix.
        set(_launcher "${CMAKE_COMMAND};-E;env;--unset=VS_UNICODE_OUTPUT;VSLANG=1033;--")
        if(CMAKE_${_language}_COMPILER_LAUNCHER)
            list(APPEND _launcher ${CMAKE_${_language}_COMPILER_LAUNCHER})
        endif()
        set(CMAKE_${_language}_COMPILER_LAUNCHER "${_launcher}")
    endforeach()
endif()
