cmake_minimum_required(VERSION 3.16)
project(BaseRender)

# Render Host 与后端共享的契约及轻量运行时服务。
add_library(BaseRender STATIC
    include/GraphicsApi.hpp
    include/GraphicsRuntime.hpp
    include/GraphicsContextHandle.hpp
    include/PresentableFrame.hpp
    include/RenderPickTypes.hpp
    include/IRenderPickCapability.hpp
    include/IRenderViewLifecycleSink.hpp
    include/IRendererSession.hpp
    include/RendererBackendDescriptor.hpp
    include/IRendererBackendFactory.hpp
    include/RendererBackendCatalog.hpp
    src/GraphicsRuntime.cpp
    src/RendererBackendCatalog.cpp
    include/FPSMonitor.hpp
    include/FrameSynchronization.hpp
    include/RefreshRateManager.hpp
)

target_include_directories(BaseRender PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(BaseRender PUBLIC
    BaseDB
    Qt6::Core
    Qt6::Gui
)

target_compile_features(BaseRender PUBLIC cxx_std_20)
set_target_properties(BaseRender PROPERTIES AUTOMOC ON)
