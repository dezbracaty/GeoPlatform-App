add_library(GPlatformServices STATIC
    DownloadManager.h
    DownloadManager.cpp
    FlashForgeModelService.h
    FlashForgeModelService.cpp
    IModelPreviewService.h
    IModelService.h
    LocalModelLibraryService.h
    LocalModelLibraryService.cpp
    ModelPreviewService.h
    ModelPreviewService.cpp
    ModelPreviewDBSession.h
    ModelPreviewDBSession.cpp
    ModelService.h
    ModelService.cpp
    ServicesRegistration.hpp
    ServicesRegistration.cpp
)
add_library(GPlatform::Services ALIAS GPlatformServices)

target_include_directories(GPlatformServices PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(GPlatformServices
    PUBLIC
        Qt6::Core
        Foundation::QmlRegistration
    PRIVATE
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Network
        AppDB
        Document
        Utils
        miniz
)

set_target_properties(GPlatformServices PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)

gplatform_register_module(
    TARGET GPlatformServices
    HEADER ServicesRegistration.hpp
    FUNCTION GPlatform::Services::registerModule
)
