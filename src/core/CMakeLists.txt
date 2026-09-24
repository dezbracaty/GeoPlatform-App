add_library(GPlatformCore STATIC
    AppInfo.h
    AppInfo.cpp
    ApplicationHelper.h
    ApplicationHelper.cpp
    CoreRegistration.hpp
    CoreRegistration.cpp
    CrashHandler.hpp
    CrashHandler.cpp
    GettextBridge.h
    GettextBridge.cpp
    SettingsHelper.h
    SettingsHelper.cpp
)
add_library(GPlatform::Core ALIAS GPlatformCore)

target_include_directories(GPlatformCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(GPlatformCore
    PUBLIC
        Qt6::Core
        Qt6::Quick
        Foundation::QmlRegistration
        BaseUI
    PRIVATE
        BaseInteraction
        Document
        GPlatform::Tr
        $<$<AND:$<BOOL:${ENABLE_BREAKPAD}>,$<BOOL:${BREAKPAD_FOUND}>>:breakpad>
)

target_compile_definitions(GPlatformCore PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
    PROJECT_HOTLOAD_ENABLED="${HOTLOAD_ENABLED}"
    $<$<BOOL:${HOTLOAD_ENABLED}>:PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..">
    $<$<NOT:$<BOOL:${HOTLOAD_ENABLED}>>:PROJECT_SOURCE_DIR="">
    $<$<AND:$<BOOL:${ENABLE_BREAKPAD}>,$<BOOL:${BREAKPAD_FOUND}>>:HAS_BREAKPAD>
)

target_include_directories(GPlatformCore PRIVATE
    $<$<AND:$<BOOL:${ENABLE_BREAKPAD}>,$<BOOL:${BREAKPAD_FOUND}>>:${BREAKPAD_INCLUDE_DIRS}>
)

set_target_properties(GPlatformCore PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)

gplatform_register_module(
    TARGET GPlatformCore
    HEADER CoreRegistration.hpp
    FUNCTION GPlatform::Core::registerModule
)
