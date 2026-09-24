cmake_minimum_required(VERSION 3.16)
project(BaseDB VERSION 1.0.0)

# RTTR 0.9.6 与 macOS SDK 26.1 的 C++20 不兼容
# 问题：rttr/type.h 中使用了 std::vector<argument> 作为默认参数，
# 但 argument 只是前向声明，C++20 的严格模板实例化检查导致编译失败
# 解决方案：macOS 平台使用 C++17，其他平台保持 C++20
if(APPLE)
    set(CMAKE_CXX_STANDARD 17)
    message(STATUS "BaseDB: Using C++17 for macOS (RTTR compatibility)")
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find required packages
find_package(Qt6 REQUIRED COMPONENTS Core Qml Gui)
find_package(spdlog REQUIRED)
find_package(Eigen3 REQUIRED)

# BaseDB library sources
set(BASEDB_HEADERS
    include/AutoRegisterDB.hpp
    include/SnapGeometry.hpp
    include/IDocumentRegistry.hpp
    include/IDocumentReadView.hpp
    include/BaseID.hpp
    include/ChangeTypes.hpp
    include/ImmediateNotifyGuard.hpp
    include/SystemTypes.hpp
    include/Transaction.hpp
    include/Transform.hpp
)

set(BASEDB_SOURCES
    src/SnapGeometry.cpp
    src/AutoRegisterDB.cpp
    src/IDocumentRegistry.cpp
    src/ImmediateNotifyGuard.cpp
    src/Transaction.cpp
    src/Transform.cpp
)

# Create the library
add_library(BaseDB STATIC ${BASEDB_SOURCES} ${BASEDB_HEADERS})
add_library(BaseDB::BaseDB ALIAS BaseDB)

# Set include directories
target_include_directories(BaseDB 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link required libraries
target_link_libraries(BaseDB
    PUBLIC
        BaseTypes
        Foundation
        Qt6::Core
        Qt6::Qml
        Qt6::Gui
        TransDB  # Link to TransDB library
        spdlog::spdlog
        Eigen3::Eigen
)

# Since this is an internal library, we don't need to install it
# Just make sure it can be used within the project
