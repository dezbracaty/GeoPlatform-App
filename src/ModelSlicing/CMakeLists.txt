cmake_minimum_required(VERSION 3.21)

# Application-domain projection from model state to slicing input. This module
# deliberately sits between AppDB and workflow consumers; the generic Slicing
# adapter remains independent of AppDB.
add_library(ModelSlicingPolicy STATIC
    include/ModelFilamentSlotResolver.hpp
    src/ModelFilamentSlotResolver.cpp
)
add_library(GPlatform::ModelSlicingPolicy ALIAS ModelSlicingPolicy)

target_compile_features(ModelSlicingPolicy PUBLIC cxx_std_17)
target_include_directories(ModelSlicingPolicy
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(ModelSlicingPolicy PRIVATE AppDB)
set_target_properties(ModelSlicingPolicy PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
