cmake_minimum_required(VERSION 3.24)
include(cmake/GPlatformBuildInputs.cmake)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(CMAKE_HOST_APPLE)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS deployment version")
    endif()
endif()
project(GPlatform VERSION 0.0.3)
include(cmake/GPlatformMsvcDependencies.cmake)
if(NOT TARGET Foundation)
    include(cmake/GPlatformSDKConsumer.cmake)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(GPlatformSDK_FOUND)
    include(cmake/ApplicationFoundation.cmake)
endif()

set(PROJECT_APP_NAME "GPlatform")
set(PROJECT_COMPANY "GPlatform")
set(PROJECT_COPYRIGHT "Copyright (c) 2024 GPlatform. All rights reserved.")
set(PROJECT_DOMAIN "com.gplatform.app")
set(PROJECT_DESCRIPTION "A versatile platform currently focused on 3D printing management with plans for future expansion.")

if(WIN32)
    set(APPLICATION_DIR_PATH ${CMAKE_CURRENT_BINARY_DIR})
    set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/favicon/favicon.ico")
elseif(APPLE)
    set(APPLICATION_DIR_PATH ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_APP_NAME}.app/Contents/MacOS)
    set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/favicon/favicon.icns")
else()
    set(APPLICATION_DIR_PATH ${CMAKE_CURRENT_BINARY_DIR})
    set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/favicon/favicon.png")
endif()

set(VERSION_STR "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH}")

set(QML_IMPORT_PATH ${CMAKE_BINARY_DIR}/qt/qml CACHE STRING "" FORCE)

if(CMAKE_HOST_WIN32)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Quick QuickControls2 Network Quick3D Concurrent Multimedia
        PATHS "${Qt6_DIR}" NO_DEFAULT_PATH)
else()
    find_package(Qt6 COMPONENTS Core Quick QuickControls2 Network Quick3D Concurrent Multimedia REQUIRED)
endif()

# Find spdlog package
find_package(spdlog REQUIRED)

# Add cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ApplicationModuleComposition)

set(QT_SDK_DIR "${Qt6_DIR}/../../..")
cmake_path(SET QT_SDK_DIR NORMALIZE ${QT_SDK_DIR})

set(QML_FILES
    qml/global/App.qml
    qml/window/MainWindow.qml
    qml/window/PageWindow.qml
    qml/window/MainScreen.qml
    qml/pages/DashboardPage.qml
    qml/pages/ModelCenterPage.qml
    qml/pages/ModelLibraryPage.qml
    qml/pages/DownloadedModelsPage.qml
    qml/pages/MultiView3DPage.qml
    qml/pages/AboutPage.qml
    qml/pages/SettingsPage.qml
    qml/pages/TransactionDebugPage.qml
    qml/pages/3DPrintPage.qml
    qml/pages/PrintWorkspacePage.qml
    qml/pages/Prepare3DPrintPage.qml
    qml/pages/Preview3DPrintPage.qml
    qml/pages/Realistic3DPrintPage.qml
    qml/pages/Generate3DPage.qml
    qml/pages/generate3d/TopBanner.qml
    qml/pages/generate3d/LeftPart.qml
    qml/pages/generate3d/RightPart.qml
    qml/dialogs/VerificationFailureDialog.qml
    qml/dialogs/SliceConfigurationErrorDialog.qml
    qml/dialogs/ZoomableImageViewer.qml
    qml/dialogs/ModelDetailPopup.qml
    qml/dialogs/PrinterControlPopup.qml
    qml/dialogs/AccountCenterPopup.qml
    qml/components/DraggablePanel.qml
    qml/components/UserGuide.qml
    qml/components/PrinterStatusBar.qml
    qml/components/MachineSelectionBar.qml
    qml/components/PreviewInspectionHint.qml
    qml/components/GlobalPrintCommandBar.qml
    qml/components/ControlMouseArea.qml
    qml/components/NotificationOverlay.qml
    qml/components/FPSDisplay.qml
    qml/components/DownloadedModels.qml
    qml/components/HoverExpandableButton.qml
    qml/components/LabeledSlider.qml
    qml/components/InspectorSection.qml
    qml/components/InspectorRow.qml
    qml/components/InspectorNumberField.qml
    qml/components/SlicingSettingsContent.qml
    qml/components/ToolpathGCodePanel.qml
    qml/components/ToolpathPreviewLegendPanel.qml
    qml/components/ToolpathPreviewSummaryContent.qml
    qml/components/ToolpathPreviewFeatureTable.qml
    qml/components/ToolpathPreviewFilamentTable.qml
    qml/components/ToolpathPreviewRangeContent.qml
    qml/components/ToolpathPreviewLayerSlider.qml
    qml/components/VerticalPushRangeSlider.qml
    qml/components/AppSurface.qml
    qml/components/ToolbarSurface.qml
    qml/components/PopoverSurface.qml
    qml/components/PrintPopoverHeader.qml
    qml/components/AppPopup.qml
    qml/components/PanelSurface.qml
    qml/components/FloatingSettingsPanel.qml
    qml/components/CameraViewAnimation.qml
    qml/components/SceneSettingsPanel.qml
    qml/components/AIAssistantPanel.qml
    qml/components/AIAssistantToggleButton.qml
    qml/components/FeatureHeaderLogo.qml
    qml/workspaces/normal/components/LeftPrintToolbar.qml
    qml/workspaces/normal/components/ModelInspectorPanel.qml
    qml/workspaces/normal/components/ModelColorPaintPanel.qml
    qml/workspaces/normal/components/FiberMaskPanel.qml
    qml/workspaces/normal/components/FiberMaskListPanel.qml
    qml/workspaces/normal/components/CutToolPanel.qml
    qml/workspaces/normal/components/RightPrintToolbar.qml
    qml/workspaces/normal/centralPart.qml
    qml/workspaces/slicing/centralPart.qml
    qml/workspaces/realistic/centralPart.qml
)

set(QML_SINGLETONS
    qml/global/Global.qml
    qml/global/PrintWorkspaceStyle.qml
)

list(APPEND QML_FILES qml/dialogs/PrinterVideoPlayer.qml)

option(GPLATFORM_PROTECT_QML_IN_RELEASE "Protect QML sources in non-debug builds" ON)

set(PACKAGED_QML_FILES ${QML_FILES})
set(PACKAGED_QML_SINGLETONS ${QML_SINGLETONS})

if(GPLATFORM_PROTECT_QML_IN_RELEASE)
    set(ENABLE_QML_PROTECTION OFF)

    if(CMAKE_CONFIGURATION_TYPES)
        # Multi-config generators (Xcode/Visual Studio): enable for all configs.
        set(ENABLE_QML_PROTECTION ON)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        set(ENABLE_QML_PROTECTION ON)
    endif()

    if(ENABLE_QML_PROTECTION)
        set(QML_PROTECTED_DIR "${CMAKE_CURRENT_BINARY_DIR}/protected_qml")
        set(QML_PROTECT_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/.cmake/protect_qml.cmake")
        set(PACKAGED_QML_FILES)
        set(PACKAGED_QML_SINGLETONS)

        foreach(qml_file IN LISTS QML_FILES QML_SINGLETONS)
            set(qml_input "${CMAKE_CURRENT_SOURCE_DIR}/${qml_file}")
            set(qml_output "${QML_PROTECTED_DIR}/${qml_file}")

            add_custom_command(
                OUTPUT "${qml_output}"
                COMMAND ${CMAKE_COMMAND}
                    -DINPUT_FILE=${qml_input}
                    -DOUTPUT_FILE=${qml_output}
                    -P "${QML_PROTECT_SCRIPT}"
                DEPENDS "${qml_input}" "${QML_PROTECT_SCRIPT}"
                COMMENT "Protecting QML file ${qml_file}"
                VERBATIM
            )

            set_source_files_properties("${qml_output}" PROPERTIES QT_RESOURCE_ALIAS "${qml_file}")
            list(FIND QML_SINGLETONS "${qml_file}" singleton_index)
            if(singleton_index GREATER -1)
                list(APPEND PACKAGED_QML_SINGLETONS "${qml_output}")
            else()
                list(APPEND PACKAGED_QML_FILES "${qml_output}")
            endif()
        endforeach()

        if(CMAKE_CONFIGURATION_TYPES)
            message(STATUS "QML source protection enabled for multi-config generator")
        else()
            message(STATUS "QML source protection enabled for ${CMAKE_BUILD_TYPE} build")
        endif()
    endif()
endif()

set(RESOURCES_FILES
    res/images/logo.png
    qml/js/PrinterManager.js
    config/default_shortcuts.json
    config/app_menus.json
    config/ai_provider.json
    config/ai_system_prompt.json
    config/slice_settings.schema.json
)

# 打印机模型文件 - 作为 Bundle Resources 分发
set(PRINTER_MODEL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/models/blackbelt_platform.STL")

# Upper-layer features, exported with the App subtree.
add_subdirectory(src/AI)
add_subdirectory(src/Slicing)
add_subdirectory(src/ModelSlicing)
add_subdirectory(src/DBRelations)
add_subdirectory(src/core)
add_subdirectory(src/services)
add_subdirectory(src/Printer)
add_subdirectory(src/Bridge)
add_subdirectory(src/Interaction)

set(GENERATED_APPLICATION_MODULE_REGISTRATION
    "${CMAKE_CURRENT_BINARY_DIR}/GeneratedApplicationModuleRegistration.cpp")
gplatform_generate_application_module_registration(
    "${GENERATED_APPLICATION_MODULE_REGISTRATION}")

set(SOURCE_FILES
    src/Application/src/main.cpp
    src/Application/include/ApplicationModuleRegistration.hpp
    ${GENERATED_APPLICATION_MODULE_REGISTRATION}
)

# Only application startup embeds the product version. Other modules read it
# through QCoreApplication so a release bump does not invalidate their objects.
set_property(SOURCE src/Application/src/main.cpp APPEND PROPERTY
    COMPILE_DEFINITIONS PROJECT_VERSION="${PROJECT_VERSION}")


set_source_files_properties(${PACKAGED_QML_SINGLETONS} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

if (WIN32)
    configure_file(.cmake/win_app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/win_app.rc)
    configure_file(.cmake/InstallerScript.iss.in ${CMAKE_SOURCE_DIR}/package/InstallerScript.iss)
    set(APP_ICON_RESOURCE_WINDOWS ${CMAKE_CURRENT_BINARY_DIR}/win_app.rc)
    qt_add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${APP_ICON_RESOURCE_WINDOWS})
elseif(APPLE)
    set(MACOSX_BUNDLE_GUI_IDENTIFIER ${PROJECT_DOMAIN})
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
    set(MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_APP_NAME})
    set(MACOSX_BUNDLE_COPYRIGHT ${PROJECT_COPYRIGHT})
    set(MACOSX_BUNDLE_DEVELOPMENT_REGION "Chinese")
    set(MACOSX_BUNDLE_EXECUTABLE_NAME ${PROJECT_APP_NAME})
    set(MACOSX_BUNDLE_ICON_FILE favicon)
    configure_file(.cmake/MacOSXBundleInfo.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)
    set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    # 打印机模型作为 Bundle Resource
    set_source_files_properties(${PRINTER_MODEL_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/models")
    qt_add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${APP_ICON} ${PRINTER_MODEL_FILE})
elseif(UNIX)
    configure_file(.cmake/linux_control.in ${CMAKE_SOURCE_DIR}/package/DEBIAN/control)
    configure_file(.cmake/linux_postinst.in ${CMAKE_SOURCE_DIR}/package/DEBIAN/postinst)
    configure_file(.cmake/linux_postrm.in ${CMAKE_SOURCE_DIR}/package/DEBIAN/postrm)
    configure_file(.cmake/linux.desktop.in ${CMAKE_SOURCE_DIR}/package/opt/${PROJECT_APP_NAME}/${PROJECT_APP_NAME}.desktop)
    qt_add_executable(${PROJECT_NAME} ${SOURCE_FILES})
else()
    qt_add_executable(${PROJECT_NAME} ${SOURCE_FILES})
endif()

gplatform_add_renderer_resources(${PROJECT_NAME})

qt_add_qml_module(${PROJECT_NAME}
    URI "GPlatform"
    VERSION "1.0"
    QML_FILES ${PACKAGED_QML_FILES} ${PACKAGED_QML_SINGLETONS}
    RESOURCES ${RESOURCES_FILES}
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qt/qml/GPlatform
)

# English is the source language and needs no catalog. Compile the two Chinese
# gettext domains and embed them under :/i18n/zh_CN/.
if(GPLATFORM_PYTHON)
    set(Python3_EXECUTABLE "${GPLATFORM_PYTHON}")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GPLATFORM_PYTHON "${Python3_EXECUTABLE}")
set(GPLATFORM_I18N_DOMAINS gplatform libslicer)
set(GPLATFORM_I18N_MO_FILES)
foreach(domain IN LISTS GPLATFORM_I18N_DOMAINS)
    set(_po "${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/zh_CN/${domain}.po")
    set(_mo "${CMAKE_CURRENT_BINARY_DIR}/i18n/zh_CN/${domain}.mo")
    add_custom_command(
        OUTPUT "${_mo}"
        COMMAND ${GPLATFORM_PYTHON} "${TR_PO2MO_SCRIPT}" "${_po}" "${_mo}"
        DEPENDS "${_po}" "${TR_PO2MO_SCRIPT}"
        COMMENT "Compiling Chinese gettext domain ${domain}"
        VERBATIM
    )
    list(APPEND GPLATFORM_I18N_MO_FILES "${_mo}")
endforeach()
add_custom_target(gplatform_i18n_mo DEPENDS ${GPLATFORM_I18N_MO_FILES})
qt_add_resources(${PROJECT_NAME} "gplatform_i18n"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${GPLATFORM_I18N_MO_FILES}
)
add_dependencies(${PROJECT_NAME} gplatform_i18n_mo)

add_custom_target(i18n_update
    COMMAND ${GPLATFORM_PYTHON} "${TR_EXTRACT_SCRIPT}"
            --src "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                  "${CMAKE_CURRENT_SOURCE_DIR}/src"
                  ${GPLATFORM_I18N_EXTRA_SOURCES}
            --source-language en
            --augment-po "${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/zh_CN/gplatform.po"
    COMMENT "Updating the Chinese application catalog from English source strings"
    VERBATIM
    USES_TERMINAL
)

# Configure common libraries (excluding Render which needs special linking)
set(COMMON_LIBRARIES
    Qt6::Quick
    Qt6::Network
    Qt6::Concurrent
    GPlatform::Tr
    Utils
    miniz
    Foundation
    Renderplugin
)

list(APPEND COMMON_LIBRARIES Qt6::Multimedia)

if(WIN32 AND TARGET libslicer::libslicer)
    file(GLOB _libslicer_private_dlls CONFIGURE_DEPENDS
        "${GPLATFORM_LIBSLICER_INSTALL_DIR}/bin/*.dll")
endif()

if(GPLATFORM_ENABLE_LIBSLICER)
    if(NOT TARGET libslicer::libslicer)
        message(FATAL_ERROR "GPLATFORM_ENABLE_LIBSLICER is ON, but libslicer::libslicer is unavailable")
    endif()
    list(APPEND COMMON_LIBRARIES libslicer::libslicer)
endif()

if (SMOOTHUI_BUILD_STATIC_LIB)
    # Static build configuration
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${COMMON_LIBRARIES}
        AppDB               # AppDB library (includes BaseDB, TransDB, and spdlog via transitive dependencies)
        Parametric          # Parametric modeling system
        Render              # Render library
        # SmoothUI static plugin targets (handled by CMake)
        SmoothUIplugin
        SmoothUIControlsplugin
        SmoothUIImplplugin
        SmoothUIDockplugin
        SmoothUIDock
        KDAB::kddockwidgets
        JournalSystem       # JournalSystem library
        $<$<AND:$<BOOL:${ENABLE_BREAKPAD}>,$<BOOL:${BREAKPAD_FOUND}>>:breakpad>  # Google Breakpad if enabled
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        SMOOTHUI_BUILD_STATIC_LIB
    )
else()
    # Shared/dynamic build configuration
    set(SHARED_LIBRARIES
        ${COMMON_LIBRARIES}
        Parametric          # Parametric modeling system
        Render              # Render library
        Qt6::Quick3D
        SmoothUI
        SmoothUIDock
        KDAB::kddockwidgets
        JournalSystem
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${SHARED_LIBRARIES}
        $<$<AND:$<BOOL:${ENABLE_BREAKPAD}>,$<BOOL:${BREAKPAD_FOUND}>>:breakpad>  # Google Breakpad if enabled
    )

endif()

gplatform_link_registered_modules(${PROJECT_NAME})

gplatform_assert_registered_renderer_application_boundary(${PROJECT_NAME})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    QT_MESSAGELOGCONTEXT
    PROJECT_NAME="${PROJECT_NAME}"
    PROJECT_APP_NAME="${PROJECT_APP_NAME}"
    PROJECT_COMPANY="${PROJECT_COMPANY}"
    PROJECT_DOMAIN="${PROJECT_DOMAIN}"
	PROJECT_DESCRIPTION="${PROJECT_DESCRIPTION}"
    $<$<BOOL:${HOTLOAD_ENABLED}>:PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}">
    $<$<NOT:$<BOOL:${HOTLOAD_ENABLED}>>:PROJECT_SOURCE_DIR="">
    PROJECT_HOTLOAD_ENABLED="${HOTLOAD_ENABLED}"
    JOURNALSYSTEM_ENABLED
    $<$<BOOL:${VTK_FOUND}>:VTK_FOUND>
    $<$<AND:$<BOOL:${ENABLE_BREAKPAD}>,$<BOOL:${BREAKPAD_FOUND}>>:HAS_BREAKPAD>
    # Enable DEBUG level logs in Debug builds
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
    # Journal playback directory mode
    $<$<CONFIG:Debug>:JOURNAL_DEBUG_MODE>
    $<$<NOT:$<CONFIG:Debug>>:JOURNAL_RELEASE_MODE>
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Application/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Printer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Bridge/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Interaction
    $<$<AND:$<BOOL:${ENABLE_BREAKPAD}>,$<BOOL:${BREAKPAD_FOUND}>>:${BREAKPAD_INCLUDE_DIRS}>
)

# MSVC specific linker options to handle VTK inline function duplicates
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE
        /FORCE:MULTIPLE  # Allow multiple symbol definitions (VTK inline functions)
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME ${PROJECT_APP_NAME}
)

if(GPLATFORM_ENABLE_LIBSLICER)
    if(APPLE)
        set(_gplatform_libslicer_runtime_dir
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Frameworks")
        set(_gplatform_libslicer_resource_dir
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/libslicer")
        set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
            BUILD_RPATH "@executable_path/../Frameworks")
        set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
            INSTALL_RPATH "@executable_path/../Frameworks")
    else()
        set(_gplatform_libslicer_runtime_dir
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
        set(_gplatform_libslicer_resource_dir
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources/libslicer")
        if(UNIX)
            set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
                BUILD_RPATH "$ORIGIN")
            set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
                INSTALL_RPATH "$ORIGIN")
        endif()
    endif()

    # A runtime- or profile-only edit may not relink the application. Track
    # both inputs so the existing deployment task also updates the runnable
    # bundle when POST_BUILD does not run.
    file(GLOB_RECURSE _gplatform_libslicer_runtime_resources
        CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
        "${LIBSLICER_RESOURCE_DIR}/*")
    set(_gplatform_libslicer_resource_stamp
        "${CMAKE_CURRENT_BINARY_DIR}/libslicer-runtime-resources.stamp")
    set(_gplatform_libslicer_private_commands "")
    if(_libslicer_private_dlls)
        list(APPEND _gplatform_libslicer_private_commands
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${_libslicer_private_dlls} "${_gplatform_libslicer_runtime_dir}")
    endif()
    add_custom_command(
        OUTPUT "${_gplatform_libslicer_resource_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${_gplatform_libslicer_resource_dir}/profiles/CFSYS"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${LIBSLICER_RESOURCE_DIR}"
            "${_gplatform_libslicer_resource_dir}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${_gplatform_libslicer_runtime_dir}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:libslicer::libslicer>"
            "${_gplatform_libslicer_runtime_dir}/$<TARGET_FILE_NAME:libslicer::libslicer>"
        ${_gplatform_libslicer_private_commands}
        COMMAND "${CMAKE_COMMAND}" -E touch
            "${_gplatform_libslicer_resource_stamp}"
        DEPENDS ${_gplatform_libslicer_runtime_resources}
            ${_libslicer_private_dlls} "$<TARGET_FILE:libslicer::libslicer>"
        COMMENT "Deploying updated libslicer runtime and preset resources"
        VERBATIM)
    add_custom_target(${PROJECT_NAME}_libslicer_resources
        DEPENDS "${_gplatform_libslicer_resource_stamp}")
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_libslicer_resources)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${_gplatform_libslicer_runtime_dir}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:libslicer::libslicer>"
            "${_gplatform_libslicer_runtime_dir}/$<TARGET_FILE_NAME:libslicer::libslicer>"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${LIBSLICER_RESOURCE_DIR}"
            "${_gplatform_libslicer_resource_dir}"
        COMMENT "Deploying libslicer runtime and owned preset resources"
        VERBATIM)

    if(APPLE)
        install(IMPORTED_RUNTIME_ARTIFACTS libslicer::libslicer
            LIBRARY DESTINATION "${PROJECT_APP_NAME}.app/Contents/Frameworks")
        install(DIRECTORY "${LIBSLICER_RESOURCE_DIR}/"
            DESTINATION "${PROJECT_APP_NAME}.app/Contents/Resources/libslicer")
    else()
        install(IMPORTED_RUNTIME_ARTIFACTS libslicer::libslicer
            RUNTIME DESTINATION "."
            LIBRARY DESTINATION ".")
        install(DIRECTORY "${LIBSLICER_RESOURCE_DIR}/"
            DESTINATION "resources/libslicer")
    endif()
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)

# Build and dist deploy from the same selected packages, never from an old bundle.
if(WIN32)
    unset(WINDEPLOYQT_EXECUTABLE CACHE)
    unset(WINDEPLOYQT_EXECUTABLE)
    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt
        PATHS "${QT_SDK_DIR}/bin" NO_DEFAULT_PATH REQUIRED)
    if(TARGET rttr_core)
        get_target_property(RTTR_DLL_LOCATION rttr_core IMPORTED_LOCATION)
    endif()

    function(_gplatform_windows_deploy_commands destination output)
        set(commands COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>" "${destination}")
        if(RTTR_DLL_LOCATION)
            list(APPEND commands COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${RTTR_DLL_LOCATION}" "${destination}")
        endif()
        if(_libslicer_private_dlls)
            list(APPEND commands COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${_libslicer_private_dlls} "${destination}")
        endif()
        if(COMMAND gplatform_get_sdk_runtime_files)
            gplatform_get_sdk_runtime_files(sdk_runtime)
            foreach(library IN LISTS sdk_runtime)
                list(APPEND commands COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${library}" "${destination}")
            endforeach()
        endif()
        # Qt deployment must run last so its output is not overwritten by raw DLLs.
        list(APPEND commands COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}" --no-translations --compiler-runtime
            "${destination}/$<TARGET_FILE_NAME:${PROJECT_NAME}>")
        set(${output} "${commands}" PARENT_SCOPE)
    endfunction()

    _gplatform_windows_deploy_commands("$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        _gplatform_build_deploy_commands)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        ${_gplatform_build_deploy_commands}
        COMMAND_EXPAND_LISTS VERBATIM
        COMMENT "Deploying Windows runtime dependencies")
endif()
set(DIST_PATH ${CMAKE_SOURCE_DIR}/dist)

file(WRITE ${CMAKE_BINARY_DIR}/remove_build_files.cmake
    "file(GLOB_RECURSE REMOVE_FILES
        \"${DIST_PATH}/*.qml\"
        \"${DIST_PATH}/*.debug\"
        \"${DIST_PATH}/*opengl32sw*\"
        \"${DIST_PATH}/*dxcompiler*\"
        \"${DIST_PATH}/*d3dcompiler_47*\"
        \"${DIST_PATH}/*vc_redist*\"
        \"${DIST_PATH}/*dxil*\"
        \"${DIST_PATH}/*Qt6Pdf*\"
        \"${DIST_PATH}/*Qt6VirtualKeyboard*\"
        \"${DIST_PATH}/*Qt6QuickControls2ImagineStyleImpl*\"
        \"${DIST_PATH}/*Qt6QuickControls2Universal*\"
        \"${DIST_PATH}/*Qt6QuickControls2UniversalStyleImpl*\"
        \"${DIST_PATH}/*Qt6QuickControls2WindowsStyleImpl*\"
        \"${DIST_PATH}/*Qt6QuickControls2Fusion*\"
        \"${DIST_PATH}/*Qt6QuickControls2FusionStyleImpl*\"
        \"${DIST_PATH}/*Qt6QuickControls2Imagine*\"
        \"${DIST_PATH}/*Qt6Quick3DUtils*\"
        \"${DIST_PATH}/*Qt63DAnimation*\"
        \"${DIST_PATH}/*Qt63DCore*\"
        \"${DIST_PATH}/*Qt63DExtras*\"
        \"${DIST_PATH}/*Qt63DInput*\"
        \"${DIST_PATH}/*Qt63DLogic*\"
        \"${DIST_PATH}/*Qt63DQuickScene2D*\"
        \"${DIST_PATH}/*Qt63DRender*\"
    )
    file(REMOVE \${REMOVE_FILES})
    "
)

if (CMAKE_BUILD_TYPE MATCHES "Release")
    if(WIN32)
        file(REAL_PATH "${CMAKE_SOURCE_DIR}" _dist_source_dir)
        set(_dist_real_path "${_dist_source_dir}/dist")
        if(EXISTS "${DIST_PATH}")
            file(REAL_PATH "${DIST_PATH}" _dist_real_path)
        endif()
        file(REAL_PATH "${CMAKE_CURRENT_BINARY_DIR}" _dist_binary_dir)
        cmake_path(IS_PREFIX _dist_real_path "${_dist_binary_dir}" NORMALIZE
            _dist_contains_app_output)
        if(IS_SYMLINK "${DIST_PATH}" OR
           NOT _dist_real_path STREQUAL "${_dist_source_dir}/dist" OR
           _dist_contains_app_output)
            message(FATAL_ERROR "dist must be the source-root output directory, separate from the application build directory")
        endif()
        set(_gplatform_dist_resource_commands "")
        if(GPLATFORM_ENABLE_LIBSLICER)
            list(APPEND _gplatform_dist_resource_commands
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${LIBSLICER_RESOURCE_DIR}" "${DIST_PATH}/resources/libslicer")
        endif()
        _gplatform_windows_deploy_commands("${DIST_PATH}"
            _gplatform_dist_deploy_commands)
        add_custom_target(${PROJECT_NAME}_dist
            COMMAND "${CMAKE_COMMAND}" -E remove_directory "${DIST_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${DIST_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:${PROJECT_NAME}>" "${DIST_PATH}"
            ${_gplatform_dist_resource_commands}
            ${_gplatform_dist_deploy_commands}
            COMMAND_EXPAND_LISTS VERBATIM
            COMMENT "Preparing complete Windows distribution"
        )
        add_dependencies(${PROJECT_NAME}_dist ${PROJECT_NAME})

        find_program(ISCC_EXECUTABLE NAMES ISCC
            HINTS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 7"
                  "$ENV{ProgramFiles}/Inno Setup 7")
        if(ISCC_EXECUTABLE AND EXISTS "${ISCC_EXECUTABLE}")
            add_custom_target(${PROJECT_NAME}_installer
                COMMAND "${ISCC_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/package/InstallerScript.iss"
                DEPENDS "${CMAKE_SOURCE_DIR}/package/InstallerScript.iss"
                        "${CMAKE_CURRENT_SOURCE_DIR}/config/GPlatform.defaults.ini"
                VERBATIM
                COMMENT "Building Windows installer with Inno Setup")
            add_dependencies(${PROJECT_NAME}_installer ${PROJECT_NAME}_dist)
        else()
            add_custom_target(${PROJECT_NAME}_installer
                COMMAND "${CMAKE_COMMAND}" -E echo
                    "Inno Setup compiler ISCC.exe was not found. Install Inno Setup 7 or set ISCC_EXECUTABLE to its full path, then configure again."
                COMMAND "${CMAKE_COMMAND}" -E false
                VERBATIM)
        endif()
endif()
if (APPLE)
    unset(QT_DEPLOY CACHE)
    unset(QT_DEPLOY)
    find_program(QT_DEPLOY NAMES macdeployqt PATHS "${QT_SDK_DIR}/bin" NO_DEFAULT_PATH)
    add_custom_target(${PROJECT_NAME}_dist
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${DIST_PATH}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_APP_NAME}.app ${DIST_PATH}/${PROJECT_APP_NAME}.app
        COMMAND ${QT_DEPLOY} ${DIST_PATH}/${PROJECT_APP_NAME}.app -qmldir=${CMAKE_SOURCE_DIR}
        COMMAND find ${DIST_PATH}/${PROJECT_APP_NAME}.app -name "*.dSYM" | xargs rm -r
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/remove_build_files.cmake
        COMMAND ${QT_DEPLOY} ${DIST_PATH}/${PROJECT_APP_NAME}.app -dmg
        COMMENT "MacOs Deploying Qt Dependencies After Build........."
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
if (LINUX)
    set(LINUXDEPLOY_URL "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage")
    set(LINUXDEPLOY_DEST "${CMAKE_BINARY_DIR}/linuxdeploy")
    if(NOT EXISTS ${LINUXDEPLOY_DEST})
        file(DOWNLOAD ${LINUXDEPLOY_URL} ${LINUXDEPLOY_DEST} SHOW_PROGRESS)
        if(UNIX)
            execute_process(COMMAND chmod +x ${LINUXDEPLOY_DEST})
        endif()
        message(STATUS "Downloaded linuxdeploy to: ${LINUXDEPLOY_DEST}")
    else()
        message(STATUS "linuxdeploy already exists at: ${LINUXDEPLOY_DEST}")
    endif()
    find_program(QT_DEPLOY NAMES linuxdeploy PATHS ${CMAKE_BINARY_DIR})
    add_custom_target(${PROJECT_NAME}_dist
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${DIST_PATH}
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_APP_NAME} ${DIST_PATH}/${PROJECT_APP_NAME}
        COMMAND ${CMAKE_COMMAND} -E copy ${APP_ICON} ${DIST_PATH}/${PROJECT_APP_NAME}.png
        COMMAND ${CMAKE_COMMAND} -E env PATH=${QT_SDK_DIR}/bin:$ENV{PATH} ${QT_DEPLOY} ${DIST_PATH}/${PROJECT_APP_NAME} -qmldir=${CMAKE_SOURCE_DIR} -unsupported-allow-new-glibc
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/remove_build_files.cmake
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${DIST_PATH} ${CMAKE_SOURCE_DIR}/package/opt/${PROJECT_APP_NAME}
        COMMAND chmod 775 ${CMAKE_SOURCE_DIR}/package/DEBIAN/postinst
        COMMAND chmod 775 ${CMAKE_SOURCE_DIR}/package/DEBIAN/postrm
        COMMENT "LINUX Deploying Qt Dependencies After Build........."
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
endif()

# ============================================================================
# TBB DLL Auto-Copy for Windows
# ============================================================================
# TBB is built as a shared library on Windows, so we need to copy the DLL files
# to the output directory after build. This ensures the executable can run without
# manual DLL copying.
# ============================================================================
if(WIN32 AND TARGET TBB::tbb)
    # Find TBB DLL files in the build directory
    # TBB builds DLLs in a subdirectory like: build/msvc_19.44_cxx_64_md_debug/
    get_target_property(TBB_IMPORTED_LOCATION TBB::tbb IMPORTED_LOCATION)
    if(TBB_IMPORTED_LOCATION)
        get_filename_component(TBB_LIB_DIR ${TBB_IMPORTED_LOCATION} DIRECTORY)
        get_filename_component(TBB_BUILD_DIR ${TBB_LIB_DIR} DIRECTORY)

        # Find TBB DLL files
        file(GLOB_RECURSE TBB_DLL_FILES
            "${TBB_BUILD_DIR}/*tbb*.dll"
        )

        if(TBB_DLL_FILES)
            message(STATUS "📦 Configuring TBB DLL auto-copy:")
            foreach(TBB_DLL ${TBB_DLL_FILES})
                get_filename_component(TBB_DLL_NAME ${TBB_DLL} NAME)
                message(STATUS "  → ${TBB_DLL_NAME}")
            endforeach()

            # Add post-build command to copy TBB DLLs to output directory
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${TBB_DLL_FILES}
                    $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMENT "📦 Copying TBB DLLs to output directory..."
                VERBATIM
            )
        else()
            message(WARNING "⚠️  TBB DLL files not found in ${TBB_BUILD_DIR}")
            message(WARNING "   You may need to manually copy TBB DLLs to the output directory")
        endif()
    else()
        message(WARNING "⚠️  TBB imported location not found, skipping DLL auto-copy")
    endif()
endif()

if(COMMAND gplatform_deploy_sdk_runtime)
    gplatform_deploy_sdk_runtime(${PROJECT_NAME})
endif()

option(GPLATFORM_BUILD_SDK_CHECKS "Verify application extensions against the binary SDK" OFF)
if(GPLATFORM_BUILD_SDK_CHECKS)
    enable_testing()
    add_executable(SDKConsumerExtensionCheck tests/SDKConsumerExtensionCheck.cpp)
    target_link_libraries(SDKConsumerExtensionCheck PRIVATE BaseDB Document BaseGeometry BaseRender Render)
    add_test(NAME SDKConsumerExtensionCheck COMMAND SDKConsumerExtensionCheck)
    set_tests_properties(SDKConsumerExtensionCheck PROPERTIES TIMEOUT 180)
    add_executable(PrimitivePickCheck tests/PrimitivePickCheck.cpp)
    target_link_libraries(PrimitivePickCheck PRIVATE AppDB Qt6::Core)
    add_test(NAME PrimitivePickCheck COMMAND PrimitivePickCheck)
    set_tests_properties(PrimitivePickCheck PROPERTIES TIMEOUT 30)
endif()
