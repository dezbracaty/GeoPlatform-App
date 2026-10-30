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
    if(EXISTS "${BREAKPAD_INSTALL_DIR}/include/google_breakpad/common/breakpad_types.h")
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

    # Advance through each stage in this invocation. include_guard(GLOBAL)
    # intentionally prevents re-including this file, including recursive calls.
    if(NOT HAS_SOURCE)
        if(NOT HAS_ZIP)
            set(BREAKPAD_DOWNLOAD_URL "https://github.com/google/breakpad/archive/refs/tags/${BREAKPAD_TAG}.tar.gz")
            message(STATUS "Breakpad: downloading ${BREAKPAD_DOWNLOAD_URL}")
            gplatform_download(
                "${BREAKPAD_DOWNLOAD_URL}"
                "${BREAKPAD_ZIP_FILE}"
                EXPECTED_HASH "SHA256=${BREAKPAD_EXPECTED_SHA256}"
                STATUS download_status
                LOG download_log
                SHOW_PROGRESS
            )
            list(GET download_status 0 status_code)
            list(GET download_status 1 status_msg)
            if(NOT status_code EQUAL 0)
                message(FATAL_ERROR "Breakpad download failed: ${status_msg}\n${download_log}")
            endif()
        endif()

        message(STATUS "Breakpad: extracting verified source archive")
        # Use an isolated directory so a previous partial extraction cannot be
        # mistaken for the newly downloaded source tree.
        set(_breakpad_extract_dir "${BREAKPAD_CACHE_DIR}/extract")
        file(REMOVE_RECURSE "${_breakpad_extract_dir}")
        file(MAKE_DIRECTORY "${_breakpad_extract_dir}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar xzf "${BREAKPAD_ZIP_FILE}"
            WORKING_DIRECTORY "${_breakpad_extract_dir}"
            RESULT_VARIABLE extract_result
            OUTPUT_QUIET
            ERROR_VARIABLE extract_error
        )
        if(NOT extract_result EQUAL 0)
            message(FATAL_ERROR "Breakpad extraction failed: ${extract_error}")
        endif()
        file(GLOB _breakpad_extracted_dirs "${_breakpad_extract_dir}/breakpad-*")
        list(LENGTH _breakpad_extracted_dirs _breakpad_extracted_count)
        if(NOT _breakpad_extracted_count EQUAL 1)
            message(FATAL_ERROR "Breakpad archive must contain exactly one source directory")
        endif()
        list(GET _breakpad_extracted_dirs 0 _breakpad_extracted_source)
        if(NOT EXISTS "${_breakpad_extracted_source}/src/google_breakpad/common/breakpad_types.h")
            message(FATAL_ERROR "Breakpad archive does not contain the expected source tree")
        endif()
        file(REMOVE_RECURSE "${BREAKPAD_SOURCE_DIR}")
        file(RENAME "${_breakpad_extracted_source}" "${BREAKPAD_SOURCE_DIR}")
        file(REMOVE_RECURSE "${_breakpad_extract_dir}")
    endif()

    if(NOT HAS_INSTALL)
        message(STATUS "Breakpad: installing headers")
        file(MAKE_DIRECTORY "${BREAKPAD_INSTALL_DIR}/include")
        file(COPY "${BREAKPAD_SOURCE_DIR}/src/" DESTINATION "${BREAKPAD_INSTALL_DIR}/include")
    endif()
    # Sources include headers relative to the src root.
    set(BREAKPAD_INCLUDE_DIRS
        "${BREAKPAD_SOURCE_DIR}/src"
        "${BREAKPAD_INSTALL_DIR}/include"
        CACHE INTERNAL "Breakpad include directories" FORCE
    )

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
