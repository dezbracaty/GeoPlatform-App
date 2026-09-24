if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE must be provided")
endif()

if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "QML input file not found: ${INPUT_FILE}")
endif()

file(READ "${INPUT_FILE}" qml_content)

# Normalize line endings first.
string(REPLACE "\r\n" "\n" qml_content "${qml_content}")
string(REPLACE "\r" "\n" qml_content "${qml_content}")

# Remove full-line single-line comments only.
string(REGEX REPLACE "^[ \t]*//[^\n]*\n" "" qml_content "${qml_content}")
string(REGEX REPLACE "\n[ \t]*//[^\n]*\n" "\n" qml_content "${qml_content}")

# Remove indentation and trailing spaces to reduce readability while keeping syntax.
string(REGEX REPLACE "\n[ \t]+" "\n" qml_content "${qml_content}")
string(REGEX REPLACE "[ \t]+\n" "\n" qml_content "${qml_content}")

# Collapse multiple blank lines.
string(REGEX REPLACE "\n\n+" "\n" qml_content "${qml_content}")

# Keep output stable.
string(REGEX REPLACE "^\n+" "" qml_content "${qml_content}")
string(REGEX REPLACE "\n+$" "\n" qml_content "${qml_content}")

get_filename_component(output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${OUTPUT_FILE}" "${qml_content}")
