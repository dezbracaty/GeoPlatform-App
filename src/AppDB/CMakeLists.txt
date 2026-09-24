cmake_minimum_required(VERSION 3.16)
project(AppDB)

# RTTR 0.9.6 与 macOS SDK 26.1 的 C++20 不兼容
# AppDB 依赖 BaseDB，而 BaseDB 使用 RTTR，因此需要保持一致的 C++ 标准
if(APPLE)
    set(CMAKE_CXX_STANDARD 17)
    message(STATUS "AppDB: Using C++17 for macOS (RTTR compatibility)")
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
if(NOT TARGET VTK::CommonCore)
    find_package(VTK REQUIRED)
endif()

# AppDB library sources
set(APPDB_HEADERS
    include/SnapPreviewDB.hpp
    include/GCodeSource.hpp
    include/AppDBRegistration.hpp
    include/ActorDB.hpp
    include/MaterialDB.hpp
    include/PBRMaterialDB.hpp
    include/CameraDB.hpp
    include/SceneBounds.hpp
    include/ViewportCoordinateSystem.hpp
    include/CoordinateTransform.hpp
    include/SphereDB.hpp
    include/CubeDB.hpp
    include/ConeDB.hpp
    include/CylinderDB.hpp
    include/ModelObjectDB.hpp
    include/ModelPartDB.hpp
    include/ModelInstanceDB.hpp
    include/ModelGeometryDB.hpp
    include/ModelGraphUtil.hpp
    include/GlbAssetData.hpp
    include/GlbDB.hpp
    include/GlbDBImporter.hpp
    include/PrintBedDB.hpp
    include/PrintBedTemplateDB.hpp
    include/WidgetDB.hpp
    include/ModelOrientationWidgetDB.hpp
    include/ModelScaleWidgetDB.hpp
    include/ModelTranslateWidgetDB.hpp
    include/CutPlaneWidgetDB.hpp
    include/CutPreviewDB.hpp
    include/SelectionBoxWidgetDB.hpp
    include/WindowDB.hpp
    include/WindowDBManager.hpp
    include/ToolpathPreviewTypes.hpp
    include/ToolpathPreviewUtils.hpp
    include/ToolpathPreviewDB.hpp
    include/SlicingConfigDB.hpp
    include/ManualSupportDB.hpp
    include/ModelSurfaceColorDB.hpp
    include/DebugActorDB.hpp
    include/TransientPolyDataActorDB.hpp
    include/SkyboxDB.hpp
    include/LightDB.hpp
)

set(APPDB_SOURCES
    src/SnapPreviewDB.cpp
    src/GCodeSource.cpp
    src/AppDBRegistration.cpp
    src/ActorDB.cpp
    src/MaterialDB.cpp
    src/PBRMaterialDB.cpp
    src/CameraDB.cpp
    src/SceneBounds.cpp
    src/ViewportCoordinateSystem.cpp
    src/CoordinateTransform.cpp
    src/SphereDB.cpp
    src/CubeDB.cpp
    src/ConeDB.cpp
    src/CylinderDB.cpp
    src/ModelObjectDB.cpp
    src/ModelPartDB.cpp
    src/ModelInstanceDB.cpp
    src/ModelGeometryDB.cpp
    src/ModelGraphUtil.cpp
    src/GlbDB.cpp
    src/GlbDBImporter.cpp
    src/PrintBedDB.cpp
    src/PrintBedTemplateDB.cpp
    src/WidgetDB.cpp
    src/ModelOrientationWidgetDB.cpp
    src/ModelScaleWidgetDB.cpp
    src/ModelTranslateWidgetDB.cpp
    src/CutPlaneWidgetDB.cpp
    src/CutPreviewDB.cpp
    src/SelectionBoxWidgetDB.cpp
    src/WindowDB.cpp
    src/WindowDBManager.cpp
    src/ToolpathPreviewUtils.cpp
    src/ToolpathPreviewDB.cpp
    src/SlicingConfigDB.cpp
    src/ManualSupportDB.cpp
    src/ModelSurfaceColorDB.cpp
    src/SkyboxDB.cpp
    src/LightDB.cpp
)

# Create the library
add_library(AppDB STATIC ${APPDB_SOURCES} ${APPDB_HEADERS})

# Set include directories
target_include_directories(AppDB
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link required libraries
target_link_libraries(AppDB
    PUBLIC
        BaseTypes
        BaseDB  # Link to BaseDB library
        Document
        ${VTK_LIBRARIES}
    PRIVATE
        BaseGeometry
)

# Add VTK definitions
if(COMMAND vtk_module_autoinit)
    vtk_module_autoinit(TARGETS AppDB MODULES ${VTK_LIBRARIES})
endif()

gplatform_register_module(
    TARGET AppDB
    HEADER AppDBRegistration.hpp
    FUNCTION GPlatform::AppDB::registerModule
)
