include_guard(GLOBAL)

set(GPLATFORM_TEST_ASSETS_SHA "" CACHE STRING
    "Complete TestAssets commit SHA for native tests (empty selects the platform branch)")
if(WIN32)
    set(_test_assets_default_ref windows)
else()
    set(_test_assets_default_ref macmini)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_test_assets_script "${CMAKE_CURRENT_LIST_DIR}/../../scripts/prepare_test_assets.py")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${_test_assets_script}"
        "--configure-default=${_test_assets_default_ref}" "--ref=${GPLATFORM_TEST_ASSETS_SHA}"
    RESULT_VARIABLE _test_assets_result
    OUTPUT_VARIABLE _test_assets_checkout
    ERROR_VARIABLE _test_assets_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _test_assets_result STREQUAL "0")
    message(FATAL_ERROR "Could not configure TestAssets (${_test_assets_result}): ${_test_assets_error}")
endif()
message(STATUS "TestAssets: ${_test_assets_checkout}")

unset(_test_assets_default_ref)
unset(_test_assets_script)
unset(_test_assets_result)
unset(_test_assets_checkout)
unset(_test_assets_error)
