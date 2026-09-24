cmake_minimum_required(VERSION 3.21)

add_library(Slicing STATIC
    include/SliceSession.hpp
    include/SliceVertexWelder.hpp
    include/SlicingBackend.hpp
    src/SliceVertexWelder.cpp
    src/SlicingBackend.cpp
)

target_compile_features(Slicing PUBLIC cxx_std_17)
target_include_directories(Slicing PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(Slicing
    PUBLIC libslicer::libslicer
    PRIVATE Foundation
)
set_target_properties(Slicing PROPERTIES POSITION_INDEPENDENT_CODE ON)
