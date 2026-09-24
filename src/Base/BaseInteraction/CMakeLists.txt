cmake_minimum_required(VERSION 3.16)
project(BaseInteraction VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find required packages
find_package(Qt6 REQUIRED COMPONENTS Core Qml)
find_package(spdlog REQUIRED)

# BaseInteraction library sources
set(BASEINTERACTION_HEADERS
    include/ActionManager.hpp
    include/ActionError.hpp
    include/InteractionInputEvent.hpp
    include/IActionHandlerBase.hpp
    include/ActionContext.hpp
    include/ActionHandlerRegistry.hpp
    include/ActionRegistration.hpp
    include/BaseInteractionRegistration.hpp
    include/ScopedTransactionContextMetadata.hpp
)

set(BASEINTERACTION_SOURCES
    src/ActionManager.cpp
    src/ActionHandlerRegistry.cpp
    src/BaseInteractionRegistration.cpp
)

# Create the library
add_library(BaseInteraction STATIC ${BASEINTERACTION_SOURCES} ${BASEINTERACTION_HEADERS})
add_library(BaseInteraction::BaseInteraction ALIAS BaseInteraction)

# Set include directories
target_include_directories(BaseInteraction
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link required libraries
target_link_libraries(BaseInteraction
    PUBLIC
        Foundation::QmlRegistration
        TransDB
        Qt6::Core
        spdlog::spdlog
)

gplatform_register_module(
    TARGET BaseInteraction
    HEADER BaseInteractionRegistration.hpp
    FUNCTION GPlatform::BaseInteraction::registerModule
)
