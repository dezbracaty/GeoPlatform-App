find_package(Qt6 REQUIRED COMPONENTS Test)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME JournalPythonClientCheck
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_CURRENT_SOURCE_DIR}/journal -p test_*.py -v)
endif()

add_executable(Model3MFIntegrationCheck
    Model3MFIntegrationCheck.cpp
    ${GENERATED_APPLICATION_MODULE_REGISTRATION})
target_compile_features(Model3MFIntegrationCheck PRIVATE cxx_std_17)
target_include_directories(Model3MFIntegrationCheck PRIVATE
    ${PROJECT_SOURCE_DIR}/src/Application/include)
target_compile_definitions(Model3MFIntegrationCheck PRIVATE
    THREE_MF_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/TestAssets/models/three_mf"
    THREE_MF_MENUS="${PROJECT_SOURCE_DIR}/config/app_menus.json")
target_link_libraries(Model3MFIntegrationCheck PRIVATE Utils Qt6::Test)
gplatform_link_registered_modules(Model3MFIntegrationCheck)
set(_project_catalogs
    "${PROJECT_BINARY_DIR}/i18n/zh_CN/gplatform.mo"
    "${PROJECT_BINARY_DIR}/i18n/zh_CN/libslicer.mo")
set_source_files_properties(${_project_catalogs} PROPERTIES GENERATED TRUE)
qt_add_resources(Model3MFIntegrationCheck "project_i18n"
    PREFIX "/" BASE "${PROJECT_BINARY_DIR}" FILES ${_project_catalogs})
add_dependencies(Model3MFIntegrationCheck gplatform_i18n_mo)
add_test(NAME Model3MFIntegrationCheck COMMAND Model3MFIntegrationCheck)
set_tests_properties(Model3MFIntegrationCheck PROPERTIES TIMEOUT 90)

add_executable(ProjectDocumentContractCheck ProjectDocumentContractCheck.cpp)
target_compile_features(ProjectDocumentContractCheck PRIVATE cxx_std_17)
target_link_libraries(ProjectDocumentContractCheck PRIVATE
    Qt6::Core AppDB DBRelations Document GPlatform::ModelSlicingPolicy)
gplatform_link_registered_modules(ProjectDocumentContractCheck DBRelations)
add_test(NAME ProjectDocumentContractCheck COMMAND ProjectDocumentContractCheck)
set_tests_properties(ProjectDocumentContractCheck PROPERTIES TIMEOUT 30)

if(WIN32)
    set_tests_properties(Model3MFIntegrationCheck ProjectDocumentContractCheck PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:GPlatform>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

# App QML/Bridge checks are owned here in source-host and binary-SDK builds.
set(_app_test_import_root "${GPLATFORM_UI_IMPORT_ROOT}")
if(NOT _app_test_import_root)
    set(_app_test_import_root "${CMAKE_BINARY_DIR}/imports")
endif()
add_executable(FloatingPrintConfigurationCheck
    FloatingPrintConfigurationCheck.cpp
    ${GENERATED_APPLICATION_MODULE_REGISTRATION})
target_compile_features(FloatingPrintConfigurationCheck PRIVATE cxx_std_17)
target_compile_definitions(FloatingPrintConfigurationCheck PRIVATE
    POPUP_QML_ROOT="${PROJECT_SOURCE_DIR}/qml"
    POPUP_IMPORTS="${_app_test_import_root}")
target_include_directories(FloatingPrintConfigurationCheck PRIVATE
    ${PROJECT_SOURCE_DIR}/src/Application/include
    ${PROJECT_SOURCE_DIR}/src/core)
gplatform_link_registered_modules(FloatingPrintConfigurationCheck)
target_link_libraries(FloatingPrintConfigurationCheck PRIVATE Qt6::Test Qt6::QuickControls2
    SmoothUIplugin SmoothUIControlsplugin SmoothUIImplplugin)
set(_floating_catalogs
    "${PROJECT_BINARY_DIR}/i18n/zh_CN/gplatform.mo"
    "${PROJECT_BINARY_DIR}/i18n/zh_CN/libslicer.mo")
set_source_files_properties(${_floating_catalogs} PROPERTIES GENERATED TRUE)
qt_add_resources(FloatingPrintConfigurationCheck "floating_config_i18n"
    PREFIX "/" BASE "${PROJECT_BINARY_DIR}" FILES ${_floating_catalogs})
add_dependencies(FloatingPrintConfigurationCheck gplatform_i18n_mo)
add_test(NAME FloatingPrintConfigurationCheck COMMAND FloatingPrintConfigurationCheck)
add_test(NAME FloatingPrintConfigurationLayoutCheck COMMAND FloatingPrintConfigurationCheck --layout)
add_test(NAME SlicingSettingRejectionCheck COMMAND FloatingPrintConfigurationCheck --setting-rejection)
set_tests_properties(FloatingPrintConfigurationCheck FloatingPrintConfigurationLayoutCheck SlicingSettingRejectionCheck PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 45)
if(WIN32)
    # Exercise real window input routing and native font metrics on Windows.
    set_tests_properties(FloatingPrintConfigurationCheck SlicingSettingRejectionCheck PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=software")
endif()

add_executable(PrintCommandBarLayoutCheck PrintCommandBarLayoutCheck.cpp)
target_compile_features(PrintCommandBarLayoutCheck PRIVATE cxx_std_17)
target_compile_definitions(PrintCommandBarLayoutCheck PRIVATE
    PRINTER_QML_ROOT="${PROJECT_SOURCE_DIR}/qml"
    PRINTER_TEST_IMPORTS="${_app_test_import_root}")
target_link_libraries(PrintCommandBarLayoutCheck PRIVATE
    Interaction
    Qt6::Test Qt6::QuickControls2
    SmoothUIplugin SmoothUIControlsplugin SmoothUIImplplugin)
add_test(NAME PrintCommandBarLayoutCheck COMMAND PrintCommandBarLayoutCheck)
set_tests_properties(PrintCommandBarLayoutCheck PROPERTIES
    # The 192 layout variants spend over 30 seconds settling even without
    # rendering overhead. Allow headroom for Rosetta and concurrent CI work.
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

if(WIN32)
    set_tests_properties(FloatingPrintConfigurationCheck FloatingPrintConfigurationLayoutCheck
        PrintCommandBarLayoutCheck SlicingSettingRejectionCheck PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:GPlatform>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    if(GPLATFORM_ENABLE_LIBSLICER)
        add_custom_target(gplatform_app_test_libslicer_resources
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${LIBSLICER_RESOURCE_DIR}"
                "$<TARGET_FILE_DIR:FloatingPrintConfigurationCheck>/resources/libslicer"
            VERBATIM)
        add_dependencies(FloatingPrintConfigurationCheck gplatform_app_test_libslicer_resources)
    endif()
endif()

# App model relation checks follow their production types and shared fixture.
add_executable(PrintBedRelationCheck
    PrintBedRelationCheck.cpp
)
set_target_properties(PrintBedRelationCheck PROPERTIES AUTOMOC ON)
target_compile_features(PrintBedRelationCheck PRIVATE cxx_std_17)
target_include_directories(PrintBedRelationCheck PRIVATE
    ${PROJECT_SOURCE_DIR}/src/Document/include
    ${PROJECT_SOURCE_DIR}/src/DBRelations
    ${PROJECT_SOURCE_DIR}/src/Base/BaseDB/include
    ${PROJECT_SOURCE_DIR}/src/AppDB/include
    "$<TARGET_PROPERTY:TransDB,INTERFACE_INCLUDE_DIRECTORIES>"
)
target_link_libraries(PrintBedRelationCheck PRIVATE
    Qt6::Core
    Qt6::Gui
    AppDB
    DBRelations
    Document
    Foundation
    spdlog::spdlog
)
gplatform_link_registered_modules(
    PrintBedRelationCheck DBRelations)
add_test(NAME PrintBedRelationCheck COMMAND PrintBedRelationCheck)

add_executable(ModelWidgetRelationCheck
    ModelWidgetRelationCheck.cpp
)
set_target_properties(ModelWidgetRelationCheck PROPERTIES AUTOMOC ON)
target_compile_features(ModelWidgetRelationCheck PRIVATE cxx_std_17)
target_include_directories(ModelWidgetRelationCheck PRIVATE
    ${PROJECT_SOURCE_DIR}/src/Document/include
    ${PROJECT_SOURCE_DIR}/src/DBRelations
    ${PROJECT_SOURCE_DIR}/src/Base/BaseDB/include
    ${PROJECT_SOURCE_DIR}/src/AppDB/include
    "$<TARGET_PROPERTY:TransDB,INTERFACE_INCLUDE_DIRECTORIES>"
)
target_link_libraries(ModelWidgetRelationCheck PRIVATE
    Qt6::Core
    Qt6::Gui
    AppDB
    DBRelations
    Document
    Foundation
    spdlog::spdlog
)
gplatform_link_registered_modules(
    ModelWidgetRelationCheck DBRelations)
add_test(NAME ModelWidgetRelationCheck COMMAND ModelWidgetRelationCheck)

add_executable(ModelSurfaceColorRelationCheck
    ModelSurfaceColorRelationCheck.cpp
)
set_target_properties(ModelSurfaceColorRelationCheck PROPERTIES AUTOMOC ON)
target_compile_features(ModelSurfaceColorRelationCheck PRIVATE cxx_std_17)
target_include_directories(ModelSurfaceColorRelationCheck PRIVATE
    ${PROJECT_SOURCE_DIR}/src/Document/include
    ${PROJECT_SOURCE_DIR}/src/DBRelations
    ${PROJECT_SOURCE_DIR}/src/Base/BaseDB/include
    ${PROJECT_SOURCE_DIR}/src/AppDB/include
    "$<TARGET_PROPERTY:TransDB,INTERFACE_INCLUDE_DIRECTORIES>"
)
target_link_libraries(ModelSurfaceColorRelationCheck PRIVATE
    Qt6::Core
    Qt6::Gui
    AppDB
    DBRelations
    Document
    Foundation
    spdlog::spdlog
)
gplatform_link_registered_modules(
    ModelSurfaceColorRelationCheck DBRelations)
add_test(NAME ModelSurfaceColorRelationCheck COMMAND ModelSurfaceColorRelationCheck)

add_executable(ManualSupportRelationCheck
    ManualSupportRelationCheck.cpp
)
set_target_properties(ManualSupportRelationCheck PROPERTIES AUTOMOC ON)
target_compile_features(ManualSupportRelationCheck PRIVATE cxx_std_17)
target_include_directories(ManualSupportRelationCheck PRIVATE
    ${PROJECT_SOURCE_DIR}/src/Document/include
    ${PROJECT_SOURCE_DIR}/src/DBRelations
    ${PROJECT_SOURCE_DIR}/src/Base/BaseDB/include
    ${PROJECT_SOURCE_DIR}/src/AppDB/include
    "$<TARGET_PROPERTY:TransDB,INTERFACE_INCLUDE_DIRECTORIES>"
)
target_link_libraries(ManualSupportRelationCheck PRIVATE
    Qt6::Core
    Qt6::Gui
    AppDB
    DBRelations
    Document
    Foundation
    spdlog::spdlog
)
gplatform_link_registered_modules(
    ManualSupportRelationCheck DBRelations)
add_test(NAME ManualSupportRelationCheck COMMAND ManualSupportRelationCheck)

# Backend integration uses private host implementations when available.
if(GPLATFORM_CORE_SOURCE_DIR AND TARGET RendererBackendVtk)
    add_executable(VtkPickIntegrationCheck
        VtkPickIntegrationCheck.cpp)
    target_compile_features(VtkPickIntegrationCheck PRIVATE cxx_std_20)
    target_include_directories(VtkPickIntegrationCheck PRIVATE
        ${GPLATFORM_CORE_SOURCE_DIR}/RendererBackends/Vtk/src
        ${GPLATFORM_CORE_SOURCE_DIR}/RendererBackends/Vtk/src/private
        ${GPLATFORM_CORE_SOURCE_DIR}/RendererBackends/Vtk/src/private/DBSync)
    target_link_libraries(VtkPickIntegrationCheck PRIVATE
        RendererBackendVtk
        GPlatform::RendererBackendSDK
        Qt6::Gui
        Qt6::OpenGL)
    gplatform_link_self_registering_modules(
        VtkPickIntegrationCheck RendererBackendVtk)
    if(COMMAND vtk_module_autoinit)
        vtk_module_autoinit(
            TARGETS VtkPickIntegrationCheck
            MODULES ${VTK_LIBRARIES})
    endif()
    add_test(NAME VtkPickIntegrationCheck COMMAND VtkPickIntegrationCheck)
    set_tests_properties(VtkPickIntegrationCheck PROPERTIES TIMEOUT 20)
endif()

if(GPLATFORM_CORE_SOURCE_DIR AND TARGET RendererBackendFilament)
    add_executable(FilamentOpenGLIntegrationCheck
        FilamentOpenGLIntegrationCheck.cpp)
    target_compile_features(FilamentOpenGLIntegrationCheck PRIVATE cxx_std_20)
    target_include_directories(FilamentOpenGLIntegrationCheck PRIVATE
        ${GPLATFORM_CORE_SOURCE_DIR}/RendererBackends/Filament/src/private)
    target_link_libraries(FilamentOpenGLIntegrationCheck PRIVATE
        RendererBackendFilament
        GPlatform::RendererBackendSDK
        DBRelations
        Qt6::Gui
        Qt6::OpenGL)
    gplatform_link_registered_modules(
        FilamentOpenGLIntegrationCheck DBRelations)
    add_test(NAME FilamentOpenGLIntegrationCheck
        COMMAND FilamentOpenGLIntegrationCheck)
    set_tests_properties(FilamentOpenGLIntegrationCheck PROPERTIES
        TIMEOUT 20)
endif()

foreach(_app_render_check VtkPickIntegrationCheck FilamentOpenGLIntegrationCheck)
    if(TARGET ${_app_render_check})
        target_include_directories(${_app_render_check} PRIVATE
            "${PROJECT_SOURCE_DIR}/src/Rendering/Vtk"
            "${PROJECT_SOURCE_DIR}/src/Rendering/Filament"
            "${PROJECT_SOURCE_DIR}/src/Rendering/Common")
        target_link_libraries(${_app_render_check} PRIVATE AppRendering DBRelations)
        gplatform_link_self_registering_modules(${_app_render_check} AppRendering)
    endif()
endforeach()

if(WIN32)
    foreach(_app_check PrintBedRelationCheck ModelWidgetRelationCheck
            ModelSurfaceColorRelationCheck ManualSupportRelationCheck
            VtkPickIntegrationCheck FilamentOpenGLIntegrationCheck)
        if(TEST ${_app_check})
            set_tests_properties(${_app_check} PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:GPlatform>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        endif()
    endforeach()
endif()
