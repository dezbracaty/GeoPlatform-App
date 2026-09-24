include_guard(GLOBAL)

# Application-side consumer of the neutral module registry owned by Foundation.
function(gplatform_generate_application_module_registration output_file)
    get_property(_headers GLOBAL PROPERTY GPLATFORM_REGISTERED_MODULE_HEADERS)
    get_property(_functions GLOBAL PROPERTY GPLATFORM_REGISTERED_MODULE_FUNCTIONS)

    list(LENGTH _headers _header_count)
    list(LENGTH _functions _function_count)
    if(NOT _header_count EQUAL _function_count)
        message(FATAL_ERROR "Module registration metadata is inconsistent")
    endif()

    set(GPLATFORM_MODULE_REGISTRATION_INCLUDES "")
    set(GPLATFORM_MODULE_REGISTRATION_CALLS "")
    if(_header_count GREATER 0)
        math(EXPR _last_index "${_header_count} - 1")
        foreach(_index RANGE 0 ${_last_index})
            list(GET _headers ${_index} _header)
            list(GET _functions ${_index} _function)
            string(APPEND GPLATFORM_MODULE_REGISTRATION_INCLUDES
                "#include <${_header}>\n")
            string(APPEND GPLATFORM_MODULE_REGISTRATION_CALLS
                "    ${_function}();\n")
        endforeach()
    endif()

    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ApplicationModuleRegistration.cpp.in"
        "${output_file}"
        @ONLY
    )
endfunction()
