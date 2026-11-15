find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(TrackedImportCompletionCheck TrackedImportCompletionCheck.cpp)
target_link_libraries(TrackedImportCompletionCheck PRIVATE BaseInteraction Qt6::Gui)
add_test(NAME TrackedImportCompletionCheck COMMAND TrackedImportCompletionCheck)
set_tests_properties(TrackedImportCompletionCheck PROPERTIES TIMEOUT 10)

add_executable(ExternalOpenRequestCheck ExternalOpenRequestCheck.cpp
    ../src/Application/src/ExternalOpenRequest.cpp)
target_include_directories(ExternalOpenRequestCheck PRIVATE
    ../src/Application/include ../src/Interaction/include)
target_link_libraries(ExternalOpenRequestCheck PRIVATE Qt6::Core)
add_test(NAME ExternalOpenRequestCheck COMMAND ExternalOpenRequestCheck)

add_executable(LocalInstanceEndpointCheck LocalInstanceEndpointCheck.cpp
    ../src/Application/src/LocalInstanceEndpoint.cpp
    ../src/Application/include/LocalInstanceEndpoint.hpp
    ../src/Application/src/ExternalOpenRequest.cpp)
target_include_directories(LocalInstanceEndpointCheck PRIVATE
    ../src/Application/include ../src/Interaction/include)
target_link_libraries(LocalInstanceEndpointCheck PRIVATE Qt6::Core Qt6::Network)
add_test(NAME LocalInstanceEndpointCheck COMMAND LocalInstanceEndpointCheck)
set_tests_properties(LocalInstanceEndpointCheck PROPERTIES TIMEOUT 30)

add_executable(ExternalDownloadCheck ExternalDownloadCheck.cpp)
target_link_libraries(ExternalDownloadCheck PRIVATE GPlatform::Services Qt6::Test Qt6::Network)
add_test(NAME ExternalDownloadCheck COMMAND ExternalDownloadCheck)
set_tests_properties(ExternalDownloadCheck PROPERTIES TIMEOUT 15)

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
    PRINTER_SWITCH_PROJECT="${CMAKE_CURRENT_SOURCE_DIR}/TestAssets/models/imported/printer-preset-switch/111.3mf"
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
    set_tests_properties(Model3MFIntegrationCheck ProjectDocumentContractCheck
        TrackedImportCompletionCheck ExternalOpenRequestCheck LocalInstanceEndpointCheck ExternalDownloadCheck PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:GPlatform>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
