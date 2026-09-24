cmake_minimum_required(VERSION 3.16)
project(Document)

# Document registry, undo/redo, and DB relation engine.
# Dependency direction: AppDB -> Document -> BaseDB -> Foundation.
if(APPLE)
    set(CMAKE_CXX_STANDARD 17)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_library(Document STATIC
    src/DocumentManager.cpp
    src/DocumentManagerReadView.cpp
    src/TempDBScope.cpp
    src/DBCopyService.cpp
    src/UndoRedoManager.cpp
    src/DBRelationRegistry.cpp
    src/DependencyRuleUpdater.cpp
    src/ChainUpdateSystem.cpp
    src/DocumentRegistration.cpp
    include/DocumentManager.hpp
    include/DocumentManagerReadView.hpp
    include/TempDBScope.hpp
    include/DBCopyService.hpp
    include/UndoRedoManager.hpp
    include/DBRelationTypes.hpp
    include/DBRelationRegistry.hpp
    include/ChainUpdater.hpp
    include/DependencyRuleUpdater.hpp
    include/ChainUpdateSystem.hpp
    include/DocumentRegistration.hpp
)

target_include_directories(Document PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Document PUBLIC
    Foundation::QmlRegistration
    BaseDB
)

set_target_properties(Document PROPERTIES POSITION_INDEPENDENT_CODE ON)

gplatform_register_module(
    TARGET Document
    HEADER DocumentRegistration.hpp
    FUNCTION GPlatform::Document::registerModule
)
