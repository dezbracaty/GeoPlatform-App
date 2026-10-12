# fetch_breakpad.cmake - Configure Google Breakpad

# 防止无限递归调用
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

# Check if ENABLE_BREAKPAD is ON and not in Debug mode
if(ENABLE_BREAKPAD AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "")
    message(STATUS "🔧 Breakpad 库缓存管理系统")

    # Breakpad 版本配置
    set(BREAKPAD_VERSION "2023.06.01")
    set(BREAKPAD_TAG "v${BREAKPAD_VERSION}")

    # Breakpad 版本与 SHA256 校验表
    set(BREAKPAD_KNOWN_SHA256_2023.06.01 "81555be3595e25e8be0fe6dd34e9490beba224296e0a8a858341e7bced67674d")

    if(NOT DEFINED BREAKPAD_KNOWN_SHA256_${BREAKPAD_VERSION})
        message(FATAL_ERROR
            "❌ Breakpad ${BREAKPAD_VERSION} 未登记 SHA256，请在 fetch_breakpad.cmake 顶部补充 BREAKPAD_KNOWN_SHA256_${BREAKPAD_VERSION}")
    endif()
    set(BREAKPAD_EXPECTED_SHA256 "${BREAKPAD_KNOWN_SHA256_${BREAKPAD_VERSION}}")

    # Breakpad 构建类型配置
    if(DEFINED BREAKPAD_BUILD_TYPE)
        set(BREAKPAD_ACTUAL_BUILD_TYPE "${BREAKPAD_BUILD_TYPE}")
    elseif(CMAKE_BUILD_TYPE)
        set(BREAKPAD_ACTUAL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    else()
        set(BREAKPAD_ACTUAL_BUILD_TYPE "Debug")
    endif()

    gplatform_resolve_fetch_cache(
        BREAKPAD "breakpad" "${BREAKPAD_VERSION}" "${BREAKPAD_ACTUAL_BUILD_TYPE}")
    set(BREAKPAD_ZIP_FILE "${BREAKPAD_CACHE_DIR}/breakpad-${BREAKPAD_TAG}.tar.gz")

    message(STATUS "   Breakpad 版本: ${BREAKPAD_VERSION}")
    message(STATUS "   Breakpad 构建类型: ${BREAKPAD_ACTUAL_BUILD_TYPE}")
    message(STATUS "   缓存根目录: ${FETCH_CACHE_DIR}")
    message(STATUS "   Breakpad 缓存目录: ${BREAKPAD_CACHE_DIR}")
    message(STATUS "   目标架构: ${FETCH_CACHE_ARCH}")
    message(STATUS "   源码目录: ${BREAKPAD_SOURCE_DIR}")
    message(STATUS "   构建目录: ${BREAKPAD_BUILD_DIR}")
    message(STATUS "   安装目录: ${BREAKPAD_INSTALL_DIR}")
    message(STATUS "")

    # 检查 Breakpad 库的状态
    set(HAS_INSTALL FALSE)
    set(HAS_SOURCE FALSE)
    set(HAS_ZIP FALSE)

    # 检查安装目录
    if(EXISTS "${BREAKPAD_INSTALL_DIR}/include/breakpad")
        set(HAS_INSTALL TRUE)
        message(STATUS "✅ 发现安装目录: ${BREAKPAD_INSTALL_DIR}")
    endif()

    # 检查源码目录
    if(EXISTS "${BREAKPAD_SOURCE_DIR}/src")
        set(HAS_SOURCE TRUE)
        message(STATUS "📄 发现源码目录: ${BREAKPAD_SOURCE_DIR}")
    endif()

    # 检查压缩包（同时校验 SHA256）
    if(EXISTS "${BREAKPAD_ZIP_FILE}")
        file(SHA256 "${BREAKPAD_ZIP_FILE}" BREAKPAD_ACTUAL_SHA256)
        if(BREAKPAD_ACTUAL_SHA256 STREQUAL "${BREAKPAD_EXPECTED_SHA256}")
            set(HAS_ZIP TRUE)
            message(STATUS "📦 发现压缩包: ${BREAKPAD_ZIP_FILE} (SHA256 校验通过)")
        else()
            message(WARNING "⚠️  缓存压缩包 SHA256 不匹配，将删除并重新下载\n"
                "   期望: ${BREAKPAD_EXPECTED_SHA256}\n"
                "   实际: ${BREAKPAD_ACTUAL_SHA256}")
            file(REMOVE "${BREAKPAD_ZIP_FILE}")
        endif()
    endif()

    # 决定操作策略
    set(BREAKPAD_STATUS "NONE")

    if(HAS_INSTALL)
        set(BREAKPAD_STATUS "INSTALLED")
        message(STATUS "🚀 将使用已安装的 Breakpad 库")
    elseif(HAS_SOURCE AND NOT HAS_INSTALL)
        set(BREAKPAD_STATUS "SOURCE_ONLY")
        message(STATUS "🔨 有源码无安装，将安装 Breakpad")
    elseif(HAS_ZIP AND NOT HAS_SOURCE)
        set(BREAKPAD_STATUS "ZIP_ONLY")
        message(STATUS "📦 只有压缩包，将解压并安装")
    else()
        set(BREAKPAD_STATUS "NONE")
        message(STATUS "⬇️  无缓存文件，将下载 Breakpad")
    endif()

    message(STATUS "📊 状态汇总: Install=${HAS_INSTALL}, Source=${HAS_SOURCE}, Zip=${HAS_ZIP}")
    message(STATUS "🎯 执行策略: ${BREAKPAD_STATUS}")

    # 根据状态执行相应操作
    if(BREAKPAD_STATUS STREQUAL "INSTALLED")
        # 直接使用已安装的 Breakpad
        message(STATUS "🚀 使用缓存的 Breakpad 库，跳过下载")

        # Breakpad source files include headers from the src root, e.g.
        # "client/mac/handler/exception_handler.h".
        set(BREAKPAD_INCLUDE_DIRS
            ${BREAKPAD_SOURCE_DIR}/src
            ${BREAKPAD_INSTALL_DIR}/include
            CACHE INTERNAL "Breakpad include directories" FORCE
        )

    elseif(BREAKPAD_STATUS STREQUAL "SOURCE_ONLY")
        # 安装源码
        message(STATUS "📦 正在安装 Breakpad...")

        # 创建安装目录
        file(MAKE_DIRECTORY "${BREAKPAD_INSTALL_DIR}/include")

        # 复制源码到安装目录（注意：src 目录下的内容，不是 src 本身）
        file(COPY "${BREAKPAD_SOURCE_DIR}/src/" DESTINATION "${BREAKPAD_INSTALL_DIR}/include")

        message(STATUS "✅ Breakpad 安装成功")

        # Breakpad source files include headers from the src root, e.g.
        # "client/mac/handler/exception_handler.h".
        set(BREAKPAD_INCLUDE_DIRS
            ${BREAKPAD_SOURCE_DIR}/src
            ${BREAKPAD_INSTALL_DIR}/include
            CACHE INTERNAL "Breakpad include directories" FORCE
        )

    elseif(BREAKPAD_STATUS STREQUAL "ZIP_ONLY")
        # 解压并安装
        message(STATUS "📦 解压 Breakpad 源码...")

        # 确保源码目录不存在
        if(EXISTS "${BREAKPAD_SOURCE_DIR}")
            file(REMOVE_RECURSE "${BREAKPAD_SOURCE_DIR}")
        endif()

        # 解压
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf ${BREAKPAD_ZIP_FILE}
            WORKING_DIRECTORY ${BREAKPAD_CACHE_DIR}
            RESULT_VARIABLE extract_result
            OUTPUT_QUIET
            ERROR_VARIABLE extract_error
        )

        if(extract_result EQUAL 0)
            # 检查解压后的目录名
            file(GLOB BREAKPAD_EXTRACTED_DIRS "${BREAKPAD_CACHE_DIR}/breakpad-*")
            list(GET BREAKPAD_EXTRACTED_DIRS 0 BREAKPAD_EXTRACTED_DIR)

            if(EXISTS "${BREAKPAD_EXTRACTED_DIR}")
                # 移动到标准源码目录
                file(RENAME "${BREAKPAD_EXTRACTED_DIR}" "${BREAKPAD_SOURCE_DIR}")
                message(STATUS "✅ Breakpad 解压成功")

                # 切换到安装模式
                set(HAS_SOURCE TRUE)
                set(BREAKPAD_STATUS "SOURCE_ONLY")

                # 递归调用处理安装
                include(${CMAKE_CURRENT_LIST_FILE})
            else()
                message(FATAL_ERROR "❌ 解压后未找到 Breakpad 目录")
            endif()
        else()
            message(WARNING "❌ Breakpad 解压失败: ${extract_error}")
            message(STATUS "🔄 删除损坏的压缩包并重新下载...")
            file(REMOVE "${BREAKPAD_ZIP_FILE}")
            set(BREAKPAD_STATUS "NONE")
            include(${CMAKE_CURRENT_LIST_FILE})
        endif()

    else()
        # 需要下载
        message(STATUS "⬇️  开始下载 Breakpad ${BREAKPAD_VERSION}...")

        # 使用 file(DOWNLOAD) 直接下载
        set(BREAKPAD_DOWNLOAD_URL "https://github.com/google/breakpad/archive/refs/tags/${BREAKPAD_TAG}.tar.gz")

        message(STATUS "📥 正在从 GitHub 下载 Breakpad...")
        message(STATUS "   URL: ${BREAKPAD_DOWNLOAD_URL}")

        file(DOWNLOAD
            ${BREAKPAD_DOWNLOAD_URL}
            ${BREAKPAD_ZIP_FILE}
            EXPECTED_HASH SHA256=${BREAKPAD_EXPECTED_SHA256}
            STATUS download_status
            LOG download_log
            SHOW_PROGRESS
        )

        list(GET download_status 0 status_code)
        list(GET download_status 1 status_msg)

        if(NOT status_code EQUAL 0)
            message(FATAL_ERROR "❌ Breakpad 下载失败: ${status_msg}\n   ${download_log}")
        endif()

        message(STATUS "✅ Breakpad 下载成功 (SHA256 校验通过)")

        # 递归调用自己来处理安装
        set(BREAKPAD_STATUS "ZIP_ONLY")
        include(${CMAKE_CURRENT_LIST_FILE})
    endif()

    # Breakpad specific configuration
    set(CMAKE_CXX_STANDARD 20 CACHE STRING "" FORCE)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

        # Platform-specific setup
    if(APPLE)
        set(BREAKPAD_CLIENT_DIR ${BREAKPAD_SOURCE_DIR}/src/client/mac)
        set(BREAKPAD_SOURCES
            ${BREAKPAD_SOURCE_DIR}/src/client/mac/handler/exception_handler.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/mac/handler/minidump_generator.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/mac/crash_generation/crash_generation_client.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/mac/string_utilities.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/mac/file_id.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/mac/macho_id.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/mac/macho_utilities.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/mac/macho_walker.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/mac/bootstrap_compat.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/mac/arch_utilities.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/mac/MachIPC.mm
            ${BREAKPAD_SOURCE_DIR}/src/common/md5.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/string_conversion.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/convert_UTF.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/simple_string_dictionary.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/minidump_file_writer.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/mac/handler/dynamic_images.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/mac/handler/breakpad_nlist_64.cc
        )

        # Add macOS specific include directories
        list(APPEND BREAKPAD_INCLUDE_DIRS
            ${BREAKPAD_SOURCE_DIR}/src
            ${BREAKPAD_INSTALL_DIR}/include
            ${BREAKPAD_INSTALL_DIR}/include/client/mac
            ${BREAKPAD_INSTALL_DIR}/include/common/mac
        )

    elseif(WIN32)
        set(BREAKPAD_CLIENT_DIR ${BREAKPAD_SOURCE_DIR}/src/client/windows)
        set(BREAKPAD_SOURCES
            ${BREAKPAD_SOURCE_DIR}/src/client/windows/handler/exception_handler.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/windows/crash_generation/crash_generation_client.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/windows/guid_string.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/windows/string_utils.cc
        )

        # Add Windows specific include directories
        list(APPEND BREAKPAD_INCLUDE_DIRS
            ${BREAKPAD_SOURCE_DIR}/src
            ${BREAKPAD_INSTALL_DIR}/include
            ${BREAKPAD_INSTALL_DIR}/include/client/windows
            ${BREAKPAD_INSTALL_DIR}/include/common/windows
        )

    elseif(UNIX AND NOT APPLE)
        set(BREAKPAD_CLIENT_DIR ${BREAKPAD_SOURCE_DIR}/src/client/linux)
        set(BREAKPAD_SOURCES
            ${BREAKPAD_SOURCE_DIR}/src/client/linux/handler/exception_handler.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/linux/handler/minidump_descriptor.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/linux/crash_generation/crash_generation_client.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/linux/minidump_writer/minidump_writer.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/linux/minidump_writer/linux_dumper.cc
            ${BREAKPAD_SOURCE_DIR}/src/client/linux/minidump_writer/linux_ptrace_dumper.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/linux/guid_creator.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/linux/file_id.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/linux/memory_mapped_file.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/linux/safe_readlink.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/string_conversion.cc
            ${BREAKPAD_SOURCE_DIR}/src/common/convert_UTF.cc
        )

        # Add Linux specific include directories
        list(APPEND BREAKPAD_INCLUDE_DIRS
            ${BREAKPAD_SOURCE_DIR}/src
            ${BREAKPAD_INSTALL_DIR}/include
            ${BREAKPAD_INSTALL_DIR}/include/client/linux
            ${BREAKPAD_INSTALL_DIR}/include/common/linux
        )
    endif()

    # Create Breakpad library
    add_library(breakpad STATIC ${BREAKPAD_SOURCES})

    # Set properties
    target_include_directories(breakpad PUBLIC ${BREAKPAD_INCLUDE_DIRS})

    # Platform specific compile definitions
    if(APPLE)
        target_compile_definitions(breakpad PRIVATE HAVE_MACH_O_NLIST_H)
    elseif(WIN32)
        target_compile_definitions(breakpad PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
        )
    endif()

    # Set compile features
    target_compile_features(breakpad PUBLIC cxx_std_20)

    # Export for parent project
    set(BREAKPAD_FOUND TRUE CACHE BOOL "Breakpad found" FORCE)
    set(BREAKPAD_INCLUDE_DIRS ${BREAKPAD_INCLUDE_DIRS} PARENT_SCOPE)

    message(STATUS "✅ Breakpad 配置成功")
    message(STATUS "")

    # Define a compile definition to indicate Breakpad is available
    add_compile_definitions(HAS_BREAKPAD)

elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Breakpad is disabled in Debug mode (for better debugging experience)")
    set(BREAKPAD_FOUND FALSE CACHE BOOL "Breakpad found" FORCE)
else()
    message(STATUS "Breakpad is disabled (ENABLE_BREAKPAD=OFF)")
    set(BREAKPAD_FOUND FALSE CACHE BOOL "Breakpad found" FORCE)
endif()
