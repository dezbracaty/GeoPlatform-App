cmake_minimum_required(VERSION 3.16)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

# Eigen3 版本配置
if(NOT DEFINED EIGEN3_VERSION)
    set(EIGEN3_VERSION "3.4.0" CACHE STRING "Eigen3 version to use")
endif()

# Eigen3 版本与 SHA256 校验表
set(EIGEN3_KNOWN_SHA256_3.4.0 "8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72")

if(NOT DEFINED EIGEN3_KNOWN_SHA256_${EIGEN3_VERSION})
    message(FATAL_ERROR
        "❌ Eigen3 ${EIGEN3_VERSION} 未登记 SHA256，请在 fetch_eigen3.cmake 顶部补充 EIGEN3_KNOWN_SHA256_${EIGEN3_VERSION}")
endif()
set(EIGEN3_EXPECTED_SHA256 "${EIGEN3_KNOWN_SHA256_${EIGEN3_VERSION}}")

# Eigen3构建类型配置 - 可以独立于主项目设置
if(DEFINED EIGEN3_BUILD_TYPE)
    set(EIGEN3_ACTUAL_BUILD_TYPE "${EIGEN3_BUILD_TYPE}")
elseif(CMAKE_BUILD_TYPE)
    set(EIGEN3_ACTUAL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
    set(EIGEN3_ACTUAL_BUILD_TYPE "Debug")
endif()

gplatform_resolve_fetch_cache(
    EIGEN3 "eigen3" "${EIGEN3_VERSION}" "${EIGEN3_ACTUAL_BUILD_TYPE}")
set(EIGEN3_ZIP_FILE "${EIGEN3_CACHE_DIR}/eigen-${EIGEN3_VERSION}.tar.gz")

message(STATUS "")
message(STATUS "🔧 Eigen3 库缓存管理系统")
message(STATUS "   Eigen3 版本: ${EIGEN3_VERSION}")
message(STATUS "   Eigen3构建类型: ${EIGEN3_ACTUAL_BUILD_TYPE}")
message(STATUS "   缓存目录: ${EIGEN3_CACHE_DIR}")
message(STATUS "   目标架构: ${FETCH_CACHE_ARCH}")
message(STATUS "   安装目录: ${EIGEN3_INSTALL_DIR}")
message(STATUS "   (Eigen3 是纯头文件库，无需编译)")
message(STATUS "")

# 检查是否已安装
set(HAS_INSTALL FALSE)
set(HAS_SOURCE FALSE)
set(HAS_ZIP FALSE)

if(EXISTS "${EIGEN3_INSTALL_DIR}/share/eigen3/cmake/Eigen3Config.cmake")
    set(HAS_INSTALL TRUE)
    message(STATUS "✅ 发现已安装的 Eigen3")
endif()

if(EXISTS "${EIGEN3_SOURCE_DIR}/CMakeLists.txt")
    set(HAS_SOURCE TRUE)
    message(STATUS "📄 发现 Eigen3 源码目录")
endif()

if(EXISTS "${EIGEN3_ZIP_FILE}")
    file(SHA256 "${EIGEN3_ZIP_FILE}" EIGEN3_ACTUAL_SHA256)
    if(EIGEN3_ACTUAL_SHA256 STREQUAL "${EIGEN3_EXPECTED_SHA256}")
        set(HAS_ZIP TRUE)
        message(STATUS "📦 发现 Eigen3 压缩包缓存 (SHA256 校验通过)")
    else()
        message(WARNING "⚠️  缓存压缩包 SHA256 不匹配，将删除并重新下载\n"
            "   期望: ${EIGEN3_EXPECTED_SHA256}\n"
            "   实际: ${EIGEN3_ACTUAL_SHA256}")
        file(REMOVE "${EIGEN3_ZIP_FILE}")
    endif()
endif()

message(STATUS "📊 状态汇总: Install=${HAS_INSTALL}, Source=${HAS_SOURCE}, Zip=${HAS_ZIP}")

# 决定操作策略
if(HAS_INSTALL)
    message(STATUS "🚀 使用缓存的 Eigen3 库，跳过下载")
    set(Eigen3_DIR "${EIGEN3_INSTALL_DIR}/share/eigen3/cmake" CACHE PATH "Eigen3 directory")
    find_package(Eigen3 REQUIRED PATHS ${Eigen3_DIR} NO_DEFAULT_PATH)
    return()

elseif(HAS_SOURCE AND NOT HAS_INSTALL)
    message(STATUS "📦 使用缓存的源码安装 Eigen3...")
    # Eigen3 是纯头文件库，直接复制安装
    file(MAKE_DIRECTORY "${EIGEN3_INSTALL_DIR}/include")
    file(MAKE_DIRECTORY "${EIGEN3_INSTALL_DIR}/share/eigen3/cmake")

    # 复制头文件
    file(COPY "${EIGEN3_SOURCE_DIR}/Eigen" DESTINATION "${EIGEN3_INSTALL_DIR}/include")
    file(COPY "${EIGEN3_SOURCE_DIR}/unsupported" DESTINATION "${EIGEN3_INSTALL_DIR}/include")

    # 创建简单的配置文件
    file(WRITE "${EIGEN3_INSTALL_DIR}/share/eigen3/cmake/Eigen3Config.cmake"
"# Eigen3 Config
get_filename_component(EIGEN3_CMAKE_DIR \"\${CMAKE_CURRENT_LIST_FILE}\" PATH)
get_filename_component(EIGEN3_INSTALL_PREFIX \"\${EIGEN3_CMAKE_DIR}/../../..\" ABSOLUTE)

set(EIGEN3_INCLUDE_DIR \"\${EIGEN3_INSTALL_PREFIX}/include\")
set(EIGEN3_INCLUDE_DIRS \"\${EIGEN3_INCLUDE_DIR}\")

if(NOT TARGET Eigen3::Eigen)
    add_library(Eigen3::Eigen INTERFACE IMPORTED)
    set_target_properties(Eigen3::Eigen PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES \"\${EIGEN3_INCLUDE_DIR}\"
    )
endif()

set(EIGEN3_FOUND TRUE)
")

    message(STATUS "✅ Eigen3 安装成功")
    set(Eigen3_DIR "${EIGEN3_INSTALL_DIR}/share/eigen3/cmake" CACHE PATH "Eigen3 directory")
    find_package(Eigen3 REQUIRED PATHS ${Eigen3_DIR} NO_DEFAULT_PATH)
    return()

elseif(HAS_ZIP AND NOT HAS_SOURCE)
    message(STATUS "📦 解压缓存的 Eigen3 压缩包...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${EIGEN3_ZIP_FILE}
        WORKING_DIRECTORY ${EIGEN3_CACHE_DIR}
        RESULT_VARIABLE extract_result
    )

    if(extract_result EQUAL 0)
        file(GLOB EIGEN3_EXTRACTED_DIRS "${EIGEN3_CACHE_DIR}/eigen-*")
        list(FILTER EIGEN3_EXTRACTED_DIRS EXCLUDE REGEX ".*\\.tar\\.gz$")
        if(EIGEN3_EXTRACTED_DIRS)
            list(GET EIGEN3_EXTRACTED_DIRS 0 EIGEN3_EXTRACTED_DIR)
            if(EXISTS "${EIGEN3_SOURCE_DIR}")
                file(REMOVE_RECURSE "${EIGEN3_SOURCE_DIR}")
            endif()
            file(RENAME "${EIGEN3_EXTRACTED_DIR}" "${EIGEN3_SOURCE_DIR}")
            message(STATUS "✅ Eigen3 解压成功")
            # 递归调用自己
            include(${CMAKE_CURRENT_LIST_FILE})
            return()
        endif()
    else()
        message(WARNING "⚠️  解压失败，将重新下载")
        file(REMOVE "${EIGEN3_ZIP_FILE}")
    endif()
endif()

# 需要下载
message(STATUS "⬇️  开始下载 Eigen3 ${EIGEN3_VERSION}...")

# 使用 file(DOWNLOAD) 直接下载，避免创建 subbuild 目录
set(EIGEN3_DOWNLOAD_URL "https://gitlab.com/libeigen/eigen/-/archive/${EIGEN3_VERSION}/eigen-${EIGEN3_VERSION}.tar.gz")

message(STATUS "📥 正在从 GitLab 下载 Eigen3 (约2MB)...")
message(STATUS "   URL: ${EIGEN3_DOWNLOAD_URL}")

file(DOWNLOAD
    ${EIGEN3_DOWNLOAD_URL}
    ${EIGEN3_ZIP_FILE}
    EXPECTED_HASH SHA256=${EIGEN3_EXPECTED_SHA256}
    STATUS download_status
    LOG download_log
    SHOW_PROGRESS
)

list(GET download_status 0 status_code)
list(GET download_status 1 status_msg)

if(NOT status_code EQUAL 0)
    message(FATAL_ERROR "❌ Eigen3 下载失败: ${status_msg}\n   ${download_log}")
endif()

message(STATUS "✅ Eigen3 下载成功 (SHA256 校验通过)")

# 解压
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xzf ${EIGEN3_ZIP_FILE}
    WORKING_DIRECTORY ${EIGEN3_CACHE_DIR}
    RESULT_VARIABLE extract_result
)

if(extract_result EQUAL 0)
    file(GLOB EIGEN3_EXTRACTED_DIRS "${EIGEN3_CACHE_DIR}/eigen-*")
    list(FILTER EIGEN3_EXTRACTED_DIRS EXCLUDE REGEX ".*\\.tar\\.gz$")
    if(EIGEN3_EXTRACTED_DIRS)
        list(GET EIGEN3_EXTRACTED_DIRS 0 EIGEN3_EXTRACTED_DIR)
        if(EXISTS "${EIGEN3_SOURCE_DIR}")
            file(REMOVE_RECURSE "${EIGEN3_SOURCE_DIR}")
        endif()
        file(RENAME "${EIGEN3_EXTRACTED_DIR}" "${EIGEN3_SOURCE_DIR}")
        # 递归调用自己来安装
        include(${CMAKE_CURRENT_LIST_FILE})
        return()
    endif()
else()
    message(FATAL_ERROR "❌ Eigen3 解压失败")
endif()

message(FATAL_ERROR "❌ Eigen3 设置失败")
