cmake_minimum_required(VERSION 3.16)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

# RTTR 版本与 SHA256 校验表（防御缓存损坏 / 上游 tag 重打 / 网络劫持）
set(RTTR_VERSION "0.9.6")
set(RTTR_KNOWN_SHA256_0.9.6 "058554f8644450185fd881a6598f9dee7ef85785cbc2bb5a5526a43225aa313f")

if(NOT DEFINED RTTR_KNOWN_SHA256_${RTTR_VERSION})
    message(FATAL_ERROR
        "❌ RTTR ${RTTR_VERSION} 未登记 SHA256，请在 fetch_rttr.cmake 顶部补充 RTTR_KNOWN_SHA256_${RTTR_VERSION}")
endif()
set(RTTR_EXPECTED_SHA256 "${RTTR_KNOWN_SHA256_${RTTR_VERSION}}")

# RTTR 构建类型配置 - 可以独立于主项目设置
if(DEFINED RTTR_BUILD_TYPE)
    set(RTTR_ACTUAL_BUILD_TYPE "${RTTR_BUILD_TYPE}")
    message(STATUS "   使用指定的RTTR构建类型: ${RTTR_ACTUAL_BUILD_TYPE}")
elseif(CMAKE_BUILD_TYPE)
    set(RTTR_ACTUAL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
    set(RTTR_ACTUAL_BUILD_TYPE "Debug")
    message(STATUS "   未指定构建类型，默认使用 Debug")
endif()

gplatform_resolve_fetch_cache(
    RTTR "rttr" "${RTTR_VERSION}" "${RTTR_ACTUAL_BUILD_TYPE}")

message(STATUS "🔧 RTTR 库缓存管理系统")
message(STATUS "   RTTR构建类型: ${RTTR_ACTUAL_BUILD_TYPE}")
message(STATUS "   缓存根目录: ${FETCH_CACHE_DIR}")
message(STATUS "   RTTR缓存目录: ${RTTR_CACHE_DIR}")
message(STATUS "   目标架构: ${FETCH_CACHE_ARCH}")
message(STATUS "   构建目录: ${RTTR_BUILD_DIR}")
message(STATUS "   安装目录: ${RTTR_INSTALL_DIR}")

# 定义文件路径
# 注意：FetchContent 下载的文件名是 URL 的最后部分，即 v0.9.6.tar.gz
set(RTTR_ZIP_FILE "${RTTR_CACHE_DIR}/v0.9.6.tar.gz")
# 检查RTTR库的状态 - 分别检查各个组件的存在性
set(HAS_INSTALL FALSE)
set(HAS_BUILD FALSE)
set(HAS_SOURCE FALSE)
set(HAS_ZIP FALSE)

# 检查安装目录
if(EXISTS "${RTTR_INSTALL_DIR}/lib")
    if(WIN32)
        file(GLOB RTTR_CORE_LIBS "${RTTR_INSTALL_DIR}/lib/*rttr_core*.lib" "${RTTR_INSTALL_DIR}/lib/*rttr_core*.dll")
    elseif(APPLE)
        file(GLOB RTTR_CORE_LIBS "${RTTR_INSTALL_DIR}/lib/*rttr_core*.dylib" "${RTTR_INSTALL_DIR}/lib/*rttr_core*.a")
    else()
        file(GLOB RTTR_CORE_LIBS "${RTTR_INSTALL_DIR}/lib/*rttr_core*.so" "${RTTR_INSTALL_DIR}/lib/*rttr_core*.a")
    endif()

    if(RTTR_CORE_LIBS AND EXISTS "${RTTR_INSTALL_DIR}/include/rttr")
        set(HAS_INSTALL TRUE)
        message(STATUS "✅ 发现安装目录: ${RTTR_INSTALL_DIR}")
    endif()
endif()

# 检查构建目录
if(EXISTS "${RTTR_BUILD_DIR}")
    if(WIN32)
        file(GLOB RTTR_BUILD_LIBS "${RTTR_BUILD_DIR}/lib/*rttr_core*.lib" "${RTTR_BUILD_DIR}/bin/*rttr_core*.dll")
    elseif(APPLE)
        file(GLOB RTTR_BUILD_LIBS "${RTTR_BUILD_DIR}/lib/*rttr_core*.dylib" "${RTTR_BUILD_DIR}/lib/*rttr_core*.a")
    else()
        file(GLOB RTTR_BUILD_LIBS "${RTTR_BUILD_DIR}/lib/*rttr_core*.so" "${RTTR_BUILD_DIR}/lib/*rttr_core*.a")
    endif()

    if(RTTR_BUILD_LIBS AND EXISTS "${RTTR_SOURCE_DIR}/src/rttr")
        set(HAS_BUILD TRUE)
        message(STATUS "✅ 发现已完成的构建目录: ${RTTR_BUILD_DIR}")
    endif()
endif()

# 检查源码目录
if(EXISTS "${RTTR_SOURCE_DIR}/src/rttr")
    set(HAS_SOURCE TRUE)
    message(STATUS "📄 发现源码目录: ${RTTR_SOURCE_DIR}")
endif()

# 检查压缩包（同时校验 SHA256，损坏的缓存直接删除避免静默使用）
if(EXISTS "${RTTR_ZIP_FILE}")
    file(SHA256 "${RTTR_ZIP_FILE}" RTTR_ACTUAL_SHA256)
    if(RTTR_ACTUAL_SHA256 STREQUAL "${RTTR_EXPECTED_SHA256}")
        set(HAS_ZIP TRUE)
        message(STATUS "📦 发现压缩包: ${RTTR_ZIP_FILE} (SHA256 校验通过)")
    else()
        message(WARNING "⚠️  缓存压缩包 SHA256 不匹配，将删除并重新下载\n"
            "   期望: ${RTTR_EXPECTED_SHA256}\n"
            "   实际: ${RTTR_ACTUAL_SHA256}")
        file(REMOVE "${RTTR_ZIP_FILE}")
    endif()
endif()

# 决定操作策略
set(RTTR_STATUS "NONE")

if(HAS_INSTALL)
    set(RTTR_STATUS "INSTALLED")
    message(STATUS "🚀 将使用已安装的RTTR库")
elseif(HAS_BUILD AND HAS_SOURCE)
    set(RTTR_STATUS "BUILT")
    message(STATUS "🚀 将使用已构建的RTTR库")
elseif(HAS_SOURCE AND NOT HAS_BUILD)
    set(RTTR_STATUS "SOURCE_ONLY")
    message(STATUS "🔨 有源码无构建，将进行构建")
elseif(HAS_ZIP AND NOT HAS_SOURCE)
    set(RTTR_STATUS "ZIP_ONLY")
    message(STATUS "📦 有压缩包，将解压并构建")
else()
    set(RTTR_STATUS "DOWNLOAD")
    message(STATUS "⬇️  开始下载和编译 RTTR 库...")
endif()

# 打印状态汇总
message(STATUS "📊 状态汇总: Install=${HAS_INSTALL}, Build=${HAS_BUILD}, Source=${HAS_SOURCE}, Zip=${HAS_ZIP}")
message(STATUS "🎯 执行策略: ${RTTR_STATUS}")

# 根据状态执行相应操作
if(RTTR_STATUS STREQUAL "INSTALLED")
    # 直接使用已安装的RTTR
    message(STATUS "🚀 使用缓存的 RTTR 库，跳过下载和编译")
    set(RTTR_DIR "${RTTR_INSTALL_DIR}/lib/cmake/rttr" CACHE PATH "RTTR directory")
    find_package(rttr CONFIG REQUIRED PATHS ${RTTR_DIR} NO_DEFAULT_PATH)
    set(RTTR_AVAILABLE TRUE CACHE BOOL "RTTR library is available")
    set(RTTR_AVAILABLE TRUE)

elseif(RTTR_STATUS STREQUAL "BUILT")
    # RTTR没有install目标，手动安装
    message(STATUS "📦 开始手动安装 RTTR...")

    # 创建安装目录结构
    file(MAKE_DIRECTORY "${RTTR_INSTALL_DIR}")
    file(MAKE_DIRECTORY "${RTTR_INSTALL_DIR}/include")
    file(MAKE_DIRECTORY "${RTTR_INSTALL_DIR}/lib")
    file(MAKE_DIRECTORY "${RTTR_INSTALL_DIR}/bin")
    file(MAKE_DIRECTORY "${RTTR_INSTALL_DIR}/lib/cmake/rttr")

    # 复制头文件
    file(COPY "${RTTR_SOURCE_DIR}/src/rttr" DESTINATION "${RTTR_INSTALL_DIR}/include")
    file(COPY "${RTTR_BUILD_DIR}/src/rttr" DESTINATION "${RTTR_INSTALL_DIR}/include" FILES_MATCHING PATTERN "*.h")

    # 复制库文件
    if(WIN32)
        file(GLOB RTTR_LIBS "${RTTR_BUILD_DIR}/lib/*.lib")
        file(GLOB RTTR_DLLS "${RTTR_BUILD_DIR}/bin/*.dll")
        if(RTTR_LIBS)
            file(COPY ${RTTR_LIBS} DESTINATION "${RTTR_INSTALL_DIR}/lib")
        endif()
        if(RTTR_DLLS)
            file(COPY ${RTTR_DLLS} DESTINATION "${RTTR_INSTALL_DIR}/bin")
        endif()
    elseif(APPLE)
        file(GLOB RTTR_LIBS "${RTTR_BUILD_DIR}/lib/*.dylib")
        if(RTTR_LIBS)
            file(COPY ${RTTR_LIBS} DESTINATION "${RTTR_INSTALL_DIR}/lib")
        endif()
    else()
        file(GLOB RTTR_LIBS "${RTTR_BUILD_DIR}/lib/*.so*")
        if(RTTR_LIBS)
            file(COPY ${RTTR_LIBS} DESTINATION "${RTTR_INSTALL_DIR}/lib")
        endif()
    endif()

    # 创建 RTTRConfig.cmake
    file(WRITE "${RTTR_INSTALL_DIR}/lib/cmake/rttr/RTTRConfig.cmake" "
# RTTR CMake配置文件
get_filename_component(RTTR_CMAKE_DIR \"\${CMAKE_CURRENT_LIST_FILE}\" PATH)
get_filename_component(RTTR_INSTALL_PREFIX \"\${RTTR_CMAKE_DIR}/../../..\" ABSOLUTE)

set(RTTR_INCLUDE_DIRS \"\${RTTR_INSTALL_PREFIX}/include\")

if(NOT TARGET rttr_core)
    add_library(rttr_core SHARED IMPORTED GLOBAL)

    if(WIN32)
        set_target_properties(rttr_core PROPERTIES
            IMPORTED_LOCATION \"\${RTTR_INSTALL_PREFIX}/bin/rttr_core.dll\"
            IMPORTED_IMPLIB \"\${RTTR_INSTALL_PREFIX}/lib/rttr_core.lib\"
            INTERFACE_INCLUDE_DIRECTORIES \"\${RTTR_INCLUDE_DIRS}\"
        )
    elseif(APPLE)
        set_target_properties(rttr_core PROPERTIES
            IMPORTED_LOCATION \"\${RTTR_INSTALL_PREFIX}/lib/librttr_core.dylib\"
            INTERFACE_INCLUDE_DIRECTORIES \"\${RTTR_INCLUDE_DIRS}\"
        )
    else()
        set_target_properties(rttr_core PROPERTIES
            IMPORTED_LOCATION \"\${RTTR_INSTALL_PREFIX}/lib/librttr_core.so\"
            INTERFACE_INCLUDE_DIRECTORIES \"\${RTTR_INCLUDE_DIRS}\"
        )
    endif()
endif()

set(RTTR_FOUND TRUE)
")

    message(STATUS "✅ RTTR 手动安装成功")
    set(RTTR_DIR "${RTTR_INSTALL_DIR}/lib/cmake/rttr" CACHE PATH "RTTR directory")
    find_package(rttr REQUIRED PATHS ${RTTR_DIR} NO_DEFAULT_PATH)
    set(RTTR_AVAILABLE TRUE CACHE BOOL "RTTR library is available")
    set(RTTR_AVAILABLE TRUE)

elseif(RTTR_STATUS STREQUAL "ZIP_ONLY")
    # 解压已有的zip文件并构建
    message(STATUS "📦 解压 RTTR 源码...")

    # 确保源码目录不存在
    if(EXISTS "${RTTR_SOURCE_DIR}")
        message(STATUS "⚠️  清理旧的源码目录...")
        file(REMOVE_RECURSE "${RTTR_SOURCE_DIR}")
    endif()

    # 解压zip文件
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${RTTR_ZIP_FILE}
        WORKING_DIRECTORY ${RTTR_CACHE_DIR}
        RESULT_VARIABLE extract_result
        OUTPUT_QUIET
        ERROR_VARIABLE extract_error
    )

    if(extract_result EQUAL 0)
        # 检查解压后的目录名（GitHub的压缩包解压后是 rttr-0.9.6）
        if(EXISTS "${RTTR_CACHE_DIR}/rttr-0.9.6")
            # 重命名为我们期望的目录名
            file(RENAME "${RTTR_CACHE_DIR}/rttr-0.9.6" "${RTTR_SOURCE_DIR}")
            message(STATUS "✅ RTTR 源码解压成功")
            # 设置状态为SOURCE_ONLY，让下面的逻辑处理编译
            set(RTTR_STATUS "SOURCE_ONLY")
        else()
            message(FATAL_ERROR "❌ 解压后未找到预期的目录")
        endif()
    else()
        message(FATAL_ERROR "❌ RTTR 解压失败: ${extract_error}")
    endif()
endif()

if(RTTR_STATUS STREQUAL "DOWNLOAD")
    # 需要下载RTTR
    message(STATUS "⬇️  开始下载 RTTR v0.9.6...")

    # 使用 file(DOWNLOAD) 直接下载，避免创建 subbuild 目录
    set(RTTR_DOWNLOAD_URL "https://github.com/rttrorg/rttr/archive/refs/tags/v0.9.6.tar.gz")

    message(STATUS "📥 正在从 GitHub 下载 RTTR (约3MB)...")
    message(STATUS "   URL: ${RTTR_DOWNLOAD_URL}")

    file(DOWNLOAD
        ${RTTR_DOWNLOAD_URL}
        ${RTTR_ZIP_FILE}
        EXPECTED_HASH SHA256=${RTTR_EXPECTED_SHA256}
        STATUS download_status
        LOG download_log
        SHOW_PROGRESS
    )

    list(GET download_status 0 status_code)
    list(GET download_status 1 status_msg)

    if(NOT status_code EQUAL 0)
        message(FATAL_ERROR "❌ RTTR 下载失败: ${status_msg}\n   ${download_log}")
    endif()

    message(STATUS "✅ RTTR 下载成功 (SHA256 校验通过)")

    # 解压
    message(STATUS "📦 解压 RTTR 源码...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${RTTR_ZIP_FILE}
        WORKING_DIRECTORY ${RTTR_CACHE_DIR}
        RESULT_VARIABLE extract_result
    )

    if(extract_result EQUAL 0)
        # 检查解压后的目录名（GitHub的压缩包解压后是 rttr-0.9.6）
        if(EXISTS "${RTTR_CACHE_DIR}/rttr-0.9.6")
            # 重命名为我们期望的目录名
            file(RENAME "${RTTR_CACHE_DIR}/rttr-0.9.6" "${RTTR_SOURCE_DIR}")
            message(STATUS "✅ RTTR 解压成功")

            # 下载后变成SOURCE_ONLY状态，继续处理
            set(RTTR_STATUS "SOURCE_ONLY")
        else()
            message(FATAL_ERROR "❌ 解压后未找到预期的目录")
        endif()
    else()
        message(FATAL_ERROR "❌ RTTR 解压失败: ${extract_result}")
    endif()
endif()

if(RTTR_STATUS STREQUAL "SOURCE_ONLY")
    # 需要构建和安装RTTR
    message(STATUS "🔨 开始构建 RTTR...")

    # Windows平台补丁：修复 bind_impl.h 中的模板语法问题
    if(WIN32)
        set(BIND_IMPL_FILE "${RTTR_SOURCE_DIR}/src/rttr/detail/registration/bind_impl.h")
        if(EXISTS "${BIND_IMPL_FILE}")
            message(STATUS "🔧 应用Windows平台补丁: 修复 bind_impl.h")
            file(READ "${BIND_IMPL_FILE}" BIND_IMPL_CONTENT)
            string(REPLACE "type::get" "type::template get" BIND_IMPL_CONTENT "${BIND_IMPL_CONTENT}")
            file(WRITE "${BIND_IMPL_FILE}" "${BIND_IMPL_CONTENT}")
            message(STATUS "✅ Windows补丁应用成功: type::get -> type::template get")
        else()
            message(WARNING "⚠️  未找到 bind_impl.h 文件，跳过Windows补丁")
        endif()
    endif()

    # 检测可用的生成器
    set(RTTR_GENERATOR "")
    set(RTTR_MAKE_PROGRAM "")

    # 优先使用 Ninja
    find_program(NINJA_EXECUTABLE ninja)
    if(NINJA_EXECUTABLE)
        set(RTTR_GENERATOR "Ninja")
        set(RTTR_MAKE_PROGRAM ${NINJA_EXECUTABLE})
        message(STATUS "🚀 使用 Ninja 生成器进行快速编译")
        message(STATUS "   Ninja路径: ${NINJA_EXECUTABLE}")
    else()
        # 如果没有 Ninja，使用与主项目相同的生成器
        if(CMAKE_GENERATOR)
            set(RTTR_GENERATOR "${CMAKE_GENERATOR}")
            message(STATUS "⚡ 使用项目生成器: ${CMAKE_GENERATOR}")
        else()
            # 默认使用 Unix Makefiles
            set(RTTR_GENERATOR "Unix Makefiles")
            message(STATUS "🔧 使用默认生成器: Unix Makefiles")
        endif()

        if(CMAKE_MAKE_PROGRAM)
            set(RTTR_MAKE_PROGRAM "${CMAKE_MAKE_PROGRAM}")
        endif()
    endif()

    # 准备生成器参数 - 注意：-G 和生成器名称必须分开
    set(GENERATOR_ARGS -G "${RTTR_GENERATOR}")
    if(RTTR_MAKE_PROGRAM)
        list(APPEND GENERATOR_ARGS -DCMAKE_MAKE_PROGRAM=${RTTR_MAKE_PROGRAM})
    endif()

    # RTTR 0.9.6 强制开启 -Werror，Apple Clang 21 新增的转换警告会因此中断构建。
    # 仅将该项警告恢复为普通警告，其他 RTTR 警告仍按上游配置处理。
    set(RTTR_COMPILER_ARGS "")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
       AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "21.0"
       AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "22.0")
        list(APPEND RTTR_COMPILER_ARGS
            "-DCMAKE_CXX_FLAGS=-Wno-error=implicit-const-int-float-conversion")
    endif()

    # 配置RTTR
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S ${RTTR_SOURCE_DIR}
            -B ${RTTR_BUILD_DIR}
            ${GENERATOR_ARGS}
            ${RTTR_COMPILER_ARGS}
            ${FETCH_CACHE_FORWARD_ARGS}
            -DCMAKE_BUILD_TYPE=${RTTR_ACTUAL_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${RTTR_INSTALL_DIR}
            -DBUILD_RTTR_DYNAMIC=ON
            -DBUILD_EXAMPLES=OFF
            -DBUILD_DOCUMENTATION=OFF
            -DBUILD_INSTALLER=OFF
            -DBUILD_PACKAGE=OFF
            -DBUILD_STATIC_LIBS=OFF
            -DBUILD_WITH_STATIC_RUNTIME_LIBS=OFF
            -DBUILD_UNIT_TESTS=OFF
            -DBUILD_BENCHMARKS=OFF
        RESULT_VARIABLE config_result
        OUTPUT_VARIABLE config_output
        ERROR_VARIABLE config_error
    )

    if(config_result EQUAL 0)
        message(STATUS "✅ RTTR 配置成功")

        # 构建RTTR
        message(STATUS "🔨 正在编译 RTTR...")

        # 获取可用的处理器数量

        # 先输出编译命令以便调试
        message(STATUS "   编译命令: ${CMAKE_COMMAND} --build ${RTTR_BUILD_DIR} --config ${RTTR_ACTUAL_BUILD_TYPE}")

        # 先检查构建目录是否存在
        if(NOT EXISTS "${RTTR_BUILD_DIR}")
            message(FATAL_ERROR "构建目录不存在: ${RTTR_BUILD_DIR}")
        endif()

        execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${RTTR_BUILD_DIR} --config ${RTTR_ACTUAL_BUILD_TYPE}
            RESULT_VARIABLE build_result
            WORKING_DIRECTORY ${RTTR_BUILD_DIR}
        )

        if(build_result EQUAL 0)
            message(STATUS "✅ RTTR 编译成功")

            # RTTR没有install目标，手动安装
            message(STATUS "📦 正在手动安装 RTTR...")

            # 创建安装目录结构
            file(MAKE_DIRECTORY "${RTTR_INSTALL_DIR}")
            file(MAKE_DIRECTORY "${RTTR_INSTALL_DIR}/include")
            file(MAKE_DIRECTORY "${RTTR_INSTALL_DIR}/lib")
            file(MAKE_DIRECTORY "${RTTR_INSTALL_DIR}/bin")
            file(MAKE_DIRECTORY "${RTTR_INSTALL_DIR}/lib/cmake/rttr")

            # 复制头文件
            file(COPY "${RTTR_SOURCE_DIR}/src/rttr" DESTINATION "${RTTR_INSTALL_DIR}/include")
            file(COPY "${RTTR_BUILD_DIR}/src/rttr" DESTINATION "${RTTR_INSTALL_DIR}/include" FILES_MATCHING PATTERN "*.h")

            # 复制库文件
            if(WIN32)
                file(GLOB RTTR_LIBS "${RTTR_BUILD_DIR}/lib/*.lib")
                file(GLOB RTTR_DLLS "${RTTR_BUILD_DIR}/bin/*.dll")
                if(RTTR_LIBS)
                    file(COPY ${RTTR_LIBS} DESTINATION "${RTTR_INSTALL_DIR}/lib")
                endif()
                if(RTTR_DLLS)
                    file(COPY ${RTTR_DLLS} DESTINATION "${RTTR_INSTALL_DIR}/bin")
                endif()
            elseif(APPLE)
                file(GLOB RTTR_LIBS "${RTTR_BUILD_DIR}/lib/*.dylib")
                if(RTTR_LIBS)
                    file(COPY ${RTTR_LIBS} DESTINATION "${RTTR_INSTALL_DIR}/lib")
                endif()
            else()
                file(GLOB RTTR_LIBS "${RTTR_BUILD_DIR}/lib/*.so*")
                if(RTTR_LIBS)
                    file(COPY ${RTTR_LIBS} DESTINATION "${RTTR_INSTALL_DIR}/lib")
                endif()
            endif()

            # 创建 RTTRConfig.cmake
            file(WRITE "${RTTR_INSTALL_DIR}/lib/cmake/rttr/RTTRConfig.cmake" "
# RTTR CMake配置文件
get_filename_component(RTTR_CMAKE_DIR \"\${CMAKE_CURRENT_LIST_FILE}\" PATH)
get_filename_component(RTTR_INSTALL_PREFIX \"\${RTTR_CMAKE_DIR}/../../..\" ABSOLUTE)

set(RTTR_INCLUDE_DIRS \"\${RTTR_INSTALL_PREFIX}/include\")

if(NOT TARGET rttr_core)
    add_library(rttr_core SHARED IMPORTED GLOBAL)

    if(WIN32)
        set_target_properties(rttr_core PROPERTIES
            IMPORTED_LOCATION \"\${RTTR_INSTALL_PREFIX}/bin/rttr_core.dll\"
            IMPORTED_IMPLIB \"\${RTTR_INSTALL_PREFIX}/lib/rttr_core.lib\"
            INTERFACE_INCLUDE_DIRECTORIES \"\${RTTR_INCLUDE_DIRS}\"
        )
    elseif(APPLE)
        set_target_properties(rttr_core PROPERTIES
            IMPORTED_LOCATION \"\${RTTR_INSTALL_PREFIX}/lib/librttr_core.dylib\"
            INTERFACE_INCLUDE_DIRECTORIES \"\${RTTR_INCLUDE_DIRS}\"
        )
    else()
        set_target_properties(rttr_core PROPERTIES
            IMPORTED_LOCATION \"\${RTTR_INSTALL_PREFIX}/lib/librttr_core.so\"
            INTERFACE_INCLUDE_DIRECTORIES \"\${RTTR_INCLUDE_DIRS}\"
        )
    endif()
endif()

set(RTTR_FOUND TRUE)
")

            message(STATUS "✅ RTTR 手动安装成功")
            set(RTTR_DIR "${RTTR_INSTALL_DIR}/lib/cmake/rttr" CACHE PATH "RTTR directory")
            find_package(rttr CONFIG REQUIRED PATHS ${RTTR_DIR} NO_DEFAULT_PATH)
            set(RTTR_AVAILABLE TRUE CACHE BOOL "RTTR library is available")
            set(RTTR_AVAILABLE TRUE)
        else()
            # 输出详细错误信息
            message(STATUS "❌ RTTR 编译失败，返回码: ${build_result}")
            message(FATAL_ERROR "RTTR 编译失败，请查看上面的错误信息")
        endif()
    else()
        message(STATUS "❌ RTTR 配置失败，返回码: ${config_result}")
        if(config_output)
            message(STATUS "配置输出:")
            message(STATUS "${config_output}")
        endif()
        if(config_error)
            message(STATUS "错误信息:")
            message(STATUS "${config_error}")
        endif()
        message(FATAL_ERROR "RTTR 配置失败，请查看上面的错误信息")
    endif()

endif()

# 设置RTTR可用标志和输出摘要
if(RTTR_AVAILABLE)
    message(STATUS "")
    message(STATUS "🎯 RTTR 库已就绪")
    message(STATUS "   版本: 0.9.6")
    message(STATUS "   安装目录: ${RTTR_INSTALL_DIR}")

    # 确保rttr_core目标存在
    if(TARGET rttr_core)
        message(STATUS "   CMake目标: rttr_core ✓")
    else()
        message(WARNING "   CMake目标: rttr_core 未找到")
    endif()
else()
    message(WARNING "⚠️  RTTR 库不可用，事务系统功能有限")
endif()

# 便利函数：将RTTR库链接到目标
function(link_rttr_to_target target_name)
    if(RTTR_AVAILABLE AND TARGET rttr_core)
        target_link_libraries(${target_name} PRIVATE rttr_core)
        target_compile_definitions(${target_name} PRIVATE RTTR_AVAILABLE)
        message(STATUS "✅ 已将 RTTR 库链接到目标: ${target_name}")
    else()
        message(STATUS "⚠️  目标 ${target_name} 将使用模拟模式")
    endif()
endfunction()

message(STATUS "📖 缓存系统使用说明:")
message(STATUS "   1. 设置环境变量: export CMAKE_FETCH_CACHE=$HOME/.cmake_fetch_cache")
message(STATUS "   2. 首次编译会下载并缓存 RTTR 库")
message(STATUS "   3. 后续编译将直接使用缓存，大幅提升速度")
message(STATUS "   4. 在代码中使用 #ifdef RTTR_AVAILABLE 来检查可用性")
