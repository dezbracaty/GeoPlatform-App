add_library(AppRendering STATIC
    AppRenderingRegistration.cpp
    include/AppRenderingRegistration.hpp
    Common/ModelRenderAppearanceResolver.cpp
    Vtk/CameraDBSync.cpp
    Vtk/CutPlaneWidgetDBSync.cpp
    Vtk/CutPreviewDBSync.cpp
    Vtk/DebugActorDBSync.cpp
    Vtk/LightDBSync.cpp
    Vtk/ManualSupportDBSync.cpp
    Vtk/ModelInstanceDBSync.cpp
    Vtk/ModelOrientationWidgetDBSync.cpp
    Vtk/ModelScaleWidgetDBSync.cpp
    Vtk/ModelTranslateWidgetDBSync.cpp
    Vtk/PrintBedDBSync.cpp
    Vtk/SelectionBoxWidgetDBSync.cpp
    Vtk/SkyboxDBSync.cpp
    Vtk/SnapPreviewDBSync.cpp
    Vtk/ToolpathBillboardMapper.cpp
    Vtk/ToolpathPreviewDBSync.cpp
    Vtk/TransientPolyDataActorDBSync.cpp
    Vtk/VtkGlbDBSync.cpp
)
target_compile_features(AppRendering PUBLIC cxx_std_20)
set_target_properties(AppRendering PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN NO)
target_include_directories(AppRendering
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE Common Vtk ${VTK_INCLUDE_DIRS})
target_link_libraries(AppRendering PRIVATE
    BaseTypes BaseRender AppDB Document Foundation Utils FacetPaintingCore ModelSlicingPolicy
    GPlatformRendererBackendSDK RendererBackendVtk Qt6::OpenGL Qt6::Concurrent ${VTK_LIBRARIES})
if(GPLATFORM_ENABLE_FILAMENT)
    target_sources(AppRendering PRIVATE
        Filament/FilamentCameraDBSync.cpp
        Filament/FilamentGlbDBSync.cpp
        Filament/FilamentLightDBSync.cpp
        Filament/FilamentManualSupportDBSync.cpp
        Filament/FilamentModelInstanceDBSync.cpp
        Filament/FilamentPolyDataDBSync.cpp
        Filament/FilamentPrintBedDBSync.cpp
        Filament/FilamentSkyboxDBSync.cpp
        Filament/FilamentSyncRegistration.cpp
    )
    target_include_directories(AppRendering PRIVATE Filament)
    target_compile_definitions(AppRendering PRIVATE GPLATFORM_ENABLE_FILAMENT)
    target_link_libraries(AppRendering PRIVATE
        RendererBackendFilament Filament::Filament Filament::gltfio_core
        Filament::uberarchive Filament::ktxreader)
endif()
if(COMMAND vtk_module_autoinit)
    vtk_module_autoinit(TARGETS AppRendering MODULES ${VTK_LIBRARIES})
endif()
gplatform_register_module(
    TARGET AppRendering
    HEADER AppRenderingRegistration.hpp
    FUNCTION GPlatform::Rendering::registerAppRendering)
# Preserve the same read-only Document boundary for source-owned adapters.
_gplatform_assert_backend_uses_document_read_view(AppRendering)
