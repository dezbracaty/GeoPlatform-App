cmake_minimum_required(VERSION 3.21)

project(BaseGeometry LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(Eigen3 REQUIRED)

add_library(BaseGeometry STATIC
    include/MeshRayQuery.hpp
    src/MeshRayQuery.cpp
    include/MeshConvexHull.hpp
    src/MeshConvexHull.cpp
)
add_library(BaseGeometry::BaseGeometry ALIAS BaseGeometry)

target_include_directories(BaseGeometry PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(BaseGeometry
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Eigen3::Eigen
    PRIVATE
        Foundation
        Qhull::qhullcpp
        Qhull::qhullstatic_r
)

target_compile_features(BaseGeometry PUBLIC cxx_std_17)
set_target_properties(BaseGeometry PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(COMMAND link_embree_to_target)
    link_embree_to_target(BaseGeometry)
elseif(TARGET embree)
    # The binary SDK exports Embree without the source dependency helper.
    target_link_libraries(BaseGeometry PRIVATE embree)
    target_compile_definitions(BaseGeometry PRIVATE GPLATFORM_HAS_EMBREE=1)
endif()
