cmake_minimum_required(VERSION 3.16)

project(BaseTypes LANGUAGES CXX)

add_library(BaseTypes INTERFACE)
add_library(BaseTypes::BaseTypes ALIAS BaseTypes)

target_include_directories(BaseTypes
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(BaseTypes INTERFACE cxx_std_17)
