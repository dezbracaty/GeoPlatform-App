# Bridge module CMakeLists.txt

# Source files
set(BRIDGE_SOURCES
    src/GCodeLineModel.cpp
    src/AIAccountBridge.cpp
    src/AIChatBridge.cpp
    src/LocalModelLibraryBridge.cpp
    src/BridgeBase.cpp
    src/ObservableBridgeBase.cpp
    src/CuraSliceSettingsBridge.cpp
    src/SliceSettingsBridge.cpp
    src/MachineGCodeUtil.cpp
    src/DBPropertyBridge.cpp
    src/SlicingHandlerBridge.cpp
    src/SlicingVisibilityBridge.cpp
    src/SlicingPreviewBridge.cpp
    src/ModelSettingsBridge.cpp
    src/ModelColorPaintBridge.cpp
    src/CutToolBridge.cpp
    src/CameraBridge.cpp
    src/PrintBedPropertyBridge.cpp
    src/ModelPropertyBridge.cpp
    src/SelectionBridge.cpp
    src/PropertyBridgeBase.cpp
    src/TransactionDebugBridge.cpp
    src/BridgeRegistration.cpp
)

# Header files
set(BRIDGE_HEADERS
    include/GCodeLineModel.hpp
    include/AIAccountBridge.hpp
    include/AIChatBridge.hpp
    include/LocalModelLibraryBridge.hpp
    include/BridgeBase.hpp
    include/ObservableBridgeBase.hpp
    include/CuraSliceSettingsBridge.hpp
    include/SliceSettingsBridge.hpp
    include/MachineGCodeUtil.hpp
    include/DBPropertyBridge.hpp
    include/SlicingHandlerBridge.hpp
    include/SlicingVisibilityBridge.hpp
    include/SlicingPreviewBridge.hpp
    include/SlicingPreviewViewDescriptors.hpp
    include/ModelSettingsBridge.hpp
    include/ModelColorPaintBridge.hpp
    include/CutToolBridge.hpp
    include/CameraBridge.hpp
    include/PrintBedPropertyBridge.hpp
    include/ModelPropertyBridge.hpp
    include/SelectionBridge.hpp
    include/PropertyBridgeBase.hpp
    include/TransactionDebugBridge.hpp
    include/BridgeRegistration.hpp
)

# Create Bridge library
add_library(Bridge STATIC
    ${BRIDGE_SOURCES}
    ${BRIDGE_HEADERS}
)

# Include directories
target_include_directories(Bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(Bridge
    PUBLIC
        SmoothUIImpl
        Qt6::Core
        Qt6::Concurrent
        Foundation::QmlRegistration
        GPlatform::AI
        Parametric
        BaseDB    # For DocumentManager and DBInstanceID (BaseDB includes spdlog)
        BaseInteraction  # For ActionManager
        AppDB
        Utils     # Shared camera navigation operations
        Slicing   # Slicer configuration/catalog application service
    PRIVATE
        DBRelations
        GPlatformServices # Local model library application service
)

# Set properties for Qt MOC
set_target_properties(Bridge PROPERTIES
    AUTOMOC ON
)

# Export include directory for other modules
set(BRIDGE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)

gplatform_register_module(
    TARGET Bridge
    HEADER BridgeRegistration.hpp
    FUNCTION GPlatform::Bridge::registerModule
)
