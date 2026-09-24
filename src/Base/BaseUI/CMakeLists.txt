cmake_minimum_required(VERSION 3.16)
project(BaseUI VERSION 1.0.0)

add_library(BaseUI STATIC
    include/BaseUIRegistration.hpp
    include/NotificationManager.h
    include/TaskStateNotifier.hpp
    src/BaseUIRegistration.cpp
    src/NotificationManager.cpp
    src/TaskStateNotifier.cpp
)
add_library(GPlatform::BaseUI ALIAS BaseUI)

target_include_directories(BaseUI
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(BaseUI
    PUBLIC
        Foundation::QmlRegistration
        Qt6::Core
)

target_compile_features(BaseUI PUBLIC cxx_std_17)
set_target_properties(BaseUI PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)

gplatform_register_module(
    TARGET BaseUI
    HEADER BaseUIRegistration.hpp
    FUNCTION GPlatform::BaseUI::registerModule
)
