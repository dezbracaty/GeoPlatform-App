include_guard(GLOBAL)
# Shared by the full-source build and the standalone SDK consumer.
set(_app_modules "${CMAKE_CURRENT_LIST_DIR}/../src")
foreach(module Base/BaseUI Base/BaseTypes Base/BaseGeometry Base/BaseDB
        Document AppDB Base/BaseInteraction Base/BaseRender)
    add_subdirectory("${_app_modules}/${module}" "${CMAKE_BINARY_DIR}/AppFramework/${module}")
endforeach()
