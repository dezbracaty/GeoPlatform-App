cmake_minimum_required(VERSION 3.16)
project(AI VERSION 1.0.0)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(AICore INTERFACE)
add_library(GPlatform::AICore ALIAS AICore)

target_include_directories(AICore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(AICore INTERFACE cxx_std_17)

add_library(AI STATIC
    src/AgentBackendCatalog.cpp
    src/CodexAgentBackend.cpp
    src/CodexAppServerClient.cpp
    include/AI/AgentBackendCatalog.hpp
    include/AI/CodexAgentBackend.hpp
    include/AI/CodexAppServerClient.hpp
    include/AI/IAgentBackend.hpp
)

add_library(GPlatform::AI ALIAS AI)
add_library(GPlatform::AIQt ALIAS AI)

target_include_directories(AI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(AI PUBLIC
    GPlatform::AICore
    Qt6::Core
    Foundation::Foundation
)

target_compile_features(AI PUBLIC cxx_std_17)
set_target_properties(AI PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)
