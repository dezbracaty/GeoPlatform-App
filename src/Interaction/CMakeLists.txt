cmake_minimum_required(VERSION 3.21)

project(Interaction)

# RTTR 0.9.6 与 macOS SDK 26.1 的 C++20 不兼容
# Interaction 依赖 AppDB/BaseDB，需要保持一致的 C++ 标准
if(APPLE)
    set(CMAKE_CXX_STANDARD 17)
    message(STATUS "Interaction: Using C++17 for macOS (RTTR compatibility)")
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Concurrent)
find_package(Eigen3 REQUIRED)

# Interaction 模块源文件
set(INTERACTION_SOURCES
    src/ToolpathInspectHandler.cpp
    src/SnapPreviewHandler.cpp
    src/SnapService.cpp
    src/AIPanelHandler.cpp
    src/InteractionRuntime.cpp
    src/InteractionRegistration.cpp
    src/CopyPasteHandler.cpp
    src/CreateGeometryHandler.cpp
    src/SlicingHandler.cpp
    src/SliceSessionBuilder.cpp
    src/SlicingPlaybackHandler.cpp
    src/DeleteHandler.cpp
    src/UndoRedoHandler.cpp
    src/PanRotateBGHandler.cpp
    src/CameraNavigationActionHandler.cpp
    src/EnvironmentSwitchHandler.cpp
    src/ViewportInputRouter.cpp
    src/UnifiedModelImportHandler.cpp
    src/FileImportHandler.cpp
    src/GCodeImportHandler.cpp
    src/LibSlicerToolpathAdapter.cpp
    src/ModelOrientationWidgetHandler.cpp
    src/ModelScaleWidgetHandler.cpp
    src/ModelTranslateWidgetHandler.cpp
    src/ModelInteractionHandler.cpp
    src/SceneQueryHandler.cpp
    src/KeyboardShortcutManager.cpp
    src/QuickActionController.cpp
    src/ScreenshotHandler.cpp
    src/GeometryExtractor.cpp
    src/CellHighlightHandler.cpp
    src/FlattenHandler.cpp
    src/AlignmentHandler.cpp
    src/BoundsCalculator.cpp
    src/TransformCalculator.cpp
    src/SceneHandler.cpp
    src/MirrorHandler.cpp
    src/ManualSupportHandler.cpp
    src/SurfaceBrushCursor.cpp
    src/SurfaceBrushProjection.cpp
    src/ModelColorPaintHandler.cpp
    src/ModelFilamentAssignmentHandler.cpp
    src/CutToolHandler.cpp
    src/ModelColorPaintComputeCoordinator.cpp
    src/ModelLibraryActionHandler.cpp
)

# Interaction 模块头文件
set(INTERACTION_HEADERS
    include/ToolpathInspectHandler.hpp
    include/SnapPreviewHandler.hpp
    include/SnapService.hpp
    include/SnapTypes.hpp
    include/AIPanelHandler.hpp
    include/InteractionRuntime.hpp
    include/InteractionRegistration.hpp
    include/StandardActionHandler.hpp
    include/StandardBackgroundHandler.hpp
    include/CopyPasteHandler.hpp
    include/CreateGeometryHandler.hpp
    include/SlicingHandler.hpp
    include/SliceSessionBuilder.hpp
    include/SlicingPlaybackHandler.hpp
    include/DeleteHandler.hpp
    include/UndoRedoHandler.hpp
    include/PanRotateBGHandler.hpp
    include/CameraNavigationActionHandler.hpp
    include/EnvironmentSwitchHandler.hpp
    include/ViewportInputRouter.hpp
    include/UnifiedModelImportHandler.hpp
    include/FileImportHandler.hpp
    include/GCodeImportHandler.hpp
    include/LibSlicerToolpathAdapter.hpp
    include/ModelOrientationWidgetHandler.hpp
    include/ModelScaleWidgetHandler.hpp
    include/ModelTranslateWidgetHandler.hpp
    include/ModelInteractionHandler.hpp
    include/SceneQueryHandler.hpp
    include/KeyboardShortcutManager.hpp
    include/QuickActionController.hpp
    include/ScreenshotHandler.hpp
    include/GeometryExtractor.hpp
    include/CellHighlightHandler.hpp
    include/FlattenHandler.hpp
    include/AlignmentHandler.hpp
    include/AlignmentTypes.hpp
    include/BoundsCalculator.hpp
    include/TransformCalculator.hpp
    include/SceneHandler.hpp
    include/MirrorHandler.hpp
    include/ManualSupportHandler.hpp
    include/SurfaceBrushCursor.hpp
    include/SurfaceBrushProjection.hpp
    include/ModelColorPaintHandler.hpp
    include/ModelFilamentAssignmentHandler.hpp
    include/CutToolHandler.hpp
    include/ModelColorPaintComputeCoordinator.hpp
    include/ModelLibraryActionHandler.hpp
)

# Pick 路由是独立的交互基础设施。单独建目标，避免测试重复编译生产源码，
# 也避免后端或 BaseRender 依赖整个 Interaction 模块。
add_library(InteractionPick STATIC
    src/PickService.cpp
    include/PickService.hpp
    include/PickTypes.hpp
)
add_library(GPlatform::InteractionPick ALIAS InteractionPick)

target_include_directories(InteractionPick
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(InteractionPick
    PUBLIC
        BaseRender
        Qt6::Core
    PRIVATE
        AppDB
        Document
        Qt6::Gui
        Foundation
)

set_target_properties(InteractionPick PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)

# 创建静态库
add_library(Interaction STATIC
    ${INTERACTION_SOURCES}
    ${INTERACTION_HEADERS}
)

# 设置包含目录
target_include_directories(Interaction
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

# 链接库
target_link_libraries(Interaction
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Concurrent
        Foundation::QmlRegistration
        Eigen3::Eigen
        BaseTypes
        FacetPaintingCore
        BaseUI           # Process-wide notifications and task state
        BaseInteraction  # BaseInteraction module for ActionManager
        GPlatform::InteractionPick
        GPlatform::ModelSlicingPolicy
        Bridge       # Bridge module for base bridge classes
        AppDB        # AppDB already includes BaseDB and BaseDB includes TransDB
        DBRelations  # AppDB domain relation names used by interaction handlers
        BaseRender
        Utils        # Utils library for STLLoader and other IO utilities
        Parametric   # Parametric modeling gateway + services
        Printer      # Printer workflow handlers and startup validation
        GPlatformServices # Handler-level application services
        SmoothUIImpl # SmoothUI implementation for MenuData access
        Slicing      # Slicing job service and libslicer adapter contract
)

# 设置属性
set_target_properties(Interaction PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)

gplatform_register_module(
    TARGET Interaction
    HEADER InteractionRegistration.hpp
    FUNCTION GPlatform::Interaction::registerModule
)
