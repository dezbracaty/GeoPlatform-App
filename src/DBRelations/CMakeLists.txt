cmake_minimum_required(VERSION 3.16)
project(DBRelations)

# AppDB 领域对象之间的声明式关系。
# 依赖方向：DBRelations -> AppDB -> Document -> BaseDB -> Foundation。
# 关系注册由 DBRelationsRegistration.cpp 暴露显式入口，主程序启动时调用。
if(APPLE)
    set(CMAKE_CXX_STANDARD 17)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(DBRelations STATIC
    DBRelationsRegistration.cpp
    DBRelationsRegistration.hpp
    DBRelationMacros.hpp
    ModelWidgetRelations.cpp
    ModelWidgetRelations.hpp
    ModelGraphRelations.cpp
    ModelGraphRelations.hpp
    ManualSupportRelations.cpp
    ManualSupportRelations.hpp
    PrintBedCollisionDetector.cpp
    PrintBedCollisionDetector.hpp
    PrintBedRelations.cpp
    PrintBedRelations.hpp
    ToolpathPreviewRelations.cpp
    ToolpathPreviewRelations.hpp
)

target_include_directories(DBRelations PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DBRelations PUBLIC
    AppDB
)

set_target_properties(DBRelations PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

gplatform_register_module(
    TARGET DBRelations
    HEADER DBRelationsRegistration.hpp
    FUNCTION GPlatform::DBRelations::registerModule
)
