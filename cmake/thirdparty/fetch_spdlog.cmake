cmake_minimum_required(VERSION 3.16)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

# spdlog版本配置 - 可以通过CMAKE选项覆盖
if(NOT DEFINED SPDLOG_VERSION)
    set(SPDLOG_VERSION "1.17.0" CACHE STRING "spdlog version to build")
endif()

# spdlog 版本与 SHA256 校验表
set(SPDLOG_KNOWN_SHA256_1.12.0 "4dccf2d10f410c1e2feaff89966bfc49a1abb29ef6f08246335b110e001e09a9")
set(SPDLOG_KNOWN_SHA256_1.17.0 "d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744")

if(NOT DEFINED SPDLOG_KNOWN_SHA256_${SPDLOG_VERSION})
    message(FATAL_ERROR
        "❌ spdlog ${SPDLOG_VERSION} 未登记 SHA256，请在 fetch_spdlog.cmake 顶部补充 SPDLOG_KNOWN_SHA256_${SPDLOG_VERSION}")
endif()
set(SPDLOG_EXPECTED_SHA256 "${SPDLOG_KNOWN_SHA256_${SPDLOG_VERSION}}")

# spdlog构建类型配置 - 可以独立于主项目设置
if(DEFINED SPDLOG_BUILD_TYPE)
    # 如果明确指定了SPDLOG_BUILD_TYPE，使用它
    set(SPDLOG_ACTUAL_BUILD_TYPE "${SPDLOG_BUILD_TYPE}")
    message(STATUS "   使用指定的spdlog构建类型: ${SPDLOG_ACTUAL_BUILD_TYPE}")
elseif(CMAKE_BUILD_TYPE)
    # 否则使用主项目的构建类型
    set(SPDLOG_ACTUAL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
    # 默认Debug
    set(SPDLOG_ACTUAL_BUILD_TYPE "Debug")
    message(STATUS "   未指定构建类型，默认使用 Debug")
endif()

gplatform_resolve_fetch_cache(
    SPDLOG "spdlog" "${SPDLOG_VERSION}" "${SPDLOG_ACTUAL_BUILD_TYPE}")
set(SPDLOG_ZIP_FILE "${SPDLOG_CACHE_DIR}/spdlog-${SPDLOG_VERSION}.tar.gz")

message(STATUS "")
message(STATUS "🔧 spdlog 库缓存管理系统")
message(STATUS "   spdlog版本: ${SPDLOG_VERSION}")
message(STATUS "   spdlog构建类型: ${SPDLOG_ACTUAL_BUILD_TYPE}")
if(DEFINED SPDLOG_BUILD_TYPE AND NOT SPDLOG_BUILD_TYPE STREQUAL CMAKE_BUILD_TYPE)
    message(STATUS "   ⚠️  注意: spdlog使用${SPDLOG_ACTUAL_BUILD_TYPE}，主项目使用${CMAKE_BUILD_TYPE}")
endif()

# 检测编译工具
find_program(NINJA_EXECUTABLE ninja)
if(NINJA_EXECUTABLE)
    message(STATUS "   编译工具: Ninja (快速编译) ⚡")
else()
    message(STATUS "   编译工具: ${CMAKE_GENERATOR}")
endif()
message(STATUS "   缓存根目录: ${FETCH_CACHE_DIR}")
message(STATUS "   spdlog缓存目录: ${SPDLOG_CACHE_DIR}")
message(STATUS "   目标架构: ${FETCH_CACHE_ARCH}")
message(STATUS "   构建目录: ${SPDLOG_BUILD_DIR}")
message(STATUS "   安装目录: ${SPDLOG_INSTALL_DIR}")
message(STATUS "")

# 检查spdlog库的状态 - 分别检查各个组件的存在性
set(HAS_INSTALL FALSE)
set(HAS_BUILD FALSE)
set(HAS_SOURCE FALSE)
set(HAS_ZIP FALSE)

# 检查各个组件
# spdlog 使用 spdlogConfig.cmake
if(EXISTS "${SPDLOG_INSTALL_DIR}/lib/cmake/spdlog/spdlogConfig.cmake")
    set(HAS_INSTALL TRUE)
    message(STATUS "✅ 发现安装目录: ${SPDLOG_INSTALL_DIR}")
endif()

# 检查构建目录 - 检查多种可能的配置文件位置
if(EXISTS "${SPDLOG_BUILD_DIR}/lib/cmake/spdlog/spdlogConfig.cmake" OR
   EXISTS "${SPDLOG_BUILD_DIR}/spdlogConfig.cmake")
    set(HAS_BUILD TRUE)
    set(BUILD_COMPLETE TRUE)
    message(STATUS "✅ 发现已完成的构建目录: ${SPDLOG_BUILD_DIR}")
elseif(EXISTS "${SPDLOG_BUILD_DIR}/CMakeCache.txt")
    set(HAS_BUILD TRUE)
    set(BUILD_COMPLETE FALSE)
    message(STATUS "📁 发现构建目录（可能未完成）: ${SPDLOG_BUILD_DIR}")
endif()

if(EXISTS "${SPDLOG_SOURCE_DIR}/CMakeLists.txt")
    set(HAS_SOURCE TRUE)
    message(STATUS "📄 发现源码目录: ${SPDLOG_SOURCE_DIR}")
endif()

if(EXISTS "${SPDLOG_ZIP_FILE}")
    file(SHA256 "${SPDLOG_ZIP_FILE}" SPDLOG_ACTUAL_SHA256)
    if(SPDLOG_ACTUAL_SHA256 STREQUAL "${SPDLOG_EXPECTED_SHA256}")
        set(HAS_ZIP TRUE)
        message(STATUS "📦 发现压缩包: ${SPDLOG_ZIP_FILE} (SHA256 校验通过)")
    else()
        message(WARNING "⚠️  缓存压缩包 SHA256 不匹配，将删除并重新下载\n"
            "   期望: ${SPDLOG_EXPECTED_SHA256}\n"
            "   实际: ${SPDLOG_ACTUAL_SHA256}")
        file(REMOVE "${SPDLOG_ZIP_FILE}")
    endif()
endif()

# 决定操作策略
set(SPDLOG_STATUS "NONE")
set(spdlog_FOUND FALSE)

# 优先级判断：install > build > source > zip > none
if(HAS_INSTALL)
    # 有install目录，直接使用
    set(SPDLOG_STATUS "INSTALLED")
    set(spdlog_FOUND TRUE)
    message(STATUS "🚀 将使用已安装的spdlog库")

elseif(HAS_BUILD AND NOT HAS_INSTALL)
    # 检查构建是否完成
    if(BUILD_COMPLETE)
        # 构建已完成，可以直接使用
        set(SPDLOG_STATUS "BUILT_COMPLETE")
        set(spdlog_FOUND TRUE)
        message(STATUS "✅ 将直接使用已编译的spdlog（无需安装）")
    else()
        # 构建未完成，需要继续
        set(SPDLOG_STATUS "BUILT_NOT_INSTALLED")
        message(STATUS "⚠️  已构建但未完成，将继续编译")
    endif()

elseif(HAS_SOURCE AND NOT HAS_BUILD)
    # 有源码但没有build，需要构建
    set(SPDLOG_STATUS "SOURCE_ONLY")
    message(STATUS "🔨 有源码无构建，将进行构建和安装")

elseif(HAS_ZIP AND NOT HAS_SOURCE)
    # 只有zip，需要解压
    set(SPDLOG_STATUS "ZIP_ONLY")
    message(STATUS "📦 只有压缩包，将解压并构建")

else()
    # 什么都没有，需要下载
    set(SPDLOG_STATUS "NONE")
    message(STATUS "⬇️  无缓存文件，将下载spdlog")
endif()

message(STATUS "📊 状态汇总: Install=${HAS_INSTALL}, Build=${HAS_BUILD}, Source=${HAS_SOURCE}, Zip=${HAS_ZIP}")
message(STATUS "🎯 执行策略: ${SPDLOG_STATUS}")

# 根据状态执行相应操作
if(SPDLOG_STATUS STREQUAL "INSTALLED")
    # 直接使用已安装的spdlog
    message(STATUS "🚀 使用缓存的 spdlog 库，跳过下载和编译")
    set(spdlog_DIR "${SPDLOG_INSTALL_DIR}/lib/cmake/spdlog" CACHE PATH "spdlog directory")
    find_package(spdlog REQUIRED PATHS ${spdlog_DIR} NO_DEFAULT_PATH)

elseif(SPDLOG_STATUS STREQUAL "BUILT_COMPLETE")
    # 直接使用构建目录中的spdlog
    message(STATUS "🚀 使用已编译的 spdlog 库（build目录），跳过安装")
    set(spdlog_DIR "${SPDLOG_BUILD_DIR}" CACHE PATH "spdlog directory")
    find_package(spdlog REQUIRED PATHS ${spdlog_DIR} NO_DEFAULT_PATH)

elseif(SPDLOG_STATUS STREQUAL "BUILT_NOT_INSTALLED")
    # 执行安装
    message(STATUS "📦 开始安装 spdlog...")

    # 先检查构建是否完整
    if(EXISTS "${SPDLOG_BUILD_DIR}/lib")
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${SPDLOG_BUILD_DIR} --target install --config ${SPDLOG_ACTUAL_BUILD_TYPE}
            RESULT_VARIABLE install_result
            WORKING_DIRECTORY ${SPDLOG_BUILD_DIR}
        )

        if(install_result EQUAL 0)
            message(STATUS "✅ spdlog 安装成功")
            set(spdlog_DIR "${SPDLOG_INSTALL_DIR}/lib/cmake/spdlog" CACHE PATH "spdlog directory")
            find_package(spdlog REQUIRED PATHS ${spdlog_DIR} NO_DEFAULT_PATH)
            set(spdlog_FOUND TRUE)
        else()
            message(WARNING "❌ spdlog 安装失败，尝试重新构建...")
            # 删除不完整的构建并重新构建
            file(REMOVE_RECURSE "${SPDLOG_BUILD_DIR}")
            set(SPDLOG_STATUS "SOURCE_ONLY")
            include(${CMAKE_CURRENT_LIST_FILE})
        endif()
    else()
        message(WARNING "⚠️  构建目录不完整，重新构建...")
        file(REMOVE_RECURSE "${SPDLOG_BUILD_DIR}")
        set(SPDLOG_STATUS "SOURCE_ONLY")
        include(${CMAKE_CURRENT_LIST_FILE})
    endif()

elseif(SPDLOG_STATUS STREQUAL "SOURCE_ONLY")
    # 需要构建和安装
    message(STATUS "🔨 开始构建 spdlog...")

    # 检测可用的生成器
    set(SPDLOG_GENERATOR "")
    set(SPDLOG_MAKE_PROGRAM "")

    # 优先使用 Ninja
    find_program(NINJA_EXECUTABLE ninja)
    if(NINJA_EXECUTABLE)
        set(SPDLOG_GENERATOR "Ninja")
        set(SPDLOG_MAKE_PROGRAM ${NINJA_EXECUTABLE})
        message(STATUS "🚀 使用 Ninja 生成器进行快速编译")
        message(STATUS "   Ninja路径: ${NINJA_EXECUTABLE}")
    else()
        # 如果没有 Ninja，使用与主项目相同的生成器
        if(CMAKE_GENERATOR)
            set(SPDLOG_GENERATOR "${CMAKE_GENERATOR}")
            message(STATUS "⚡ 使用项目生成器: ${CMAKE_GENERATOR}")
        else()
            # 默认使用 Unix Makefiles
            set(SPDLOG_GENERATOR "Unix Makefiles")
            message(STATUS "🔧 使用默认生成器: Unix Makefiles")
        endif()

        if(CMAKE_MAKE_PROGRAM)
            set(SPDLOG_MAKE_PROGRAM "${CMAKE_MAKE_PROGRAM}")
        endif()
    endif()

    # 准备生成器参数 - 注意：-G 和生成器名称必须分开
    set(GENERATOR_ARGS -G "${SPDLOG_GENERATOR}")
    if(SPDLOG_MAKE_PROGRAM)
        list(APPEND GENERATOR_ARGS -DCMAKE_MAKE_PROGRAM=${SPDLOG_MAKE_PROGRAM})
    endif()

    # 配置spdlog
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S ${SPDLOG_SOURCE_DIR}
            -B ${SPDLOG_BUILD_DIR}
            ${GENERATOR_ARGS}
            ${FETCH_CACHE_FORWARD_ARGS}
            -DCMAKE_BUILD_TYPE=${SPDLOG_ACTUAL_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${SPDLOG_INSTALL_DIR}
            -DSPDLOG_BUILD_SHARED=OFF
            -DSPDLOG_BUILD_EXAMPLE=OFF
            -DSPDLOG_BUILD_EXAMPLE_HO=OFF
            -DSPDLOG_BUILD_TESTS=OFF
            -DSPDLOG_BUILD_TESTS_HO=OFF
            -DSPDLOG_BUILD_BENCH=OFF
            -DSPDLOG_INSTALL=ON
            -DSPDLOG_FMT_EXTERNAL=OFF
            -DSPDLOG_FMT_EXTERNAL_HO=OFF
            -DSPDLOG_NO_EXCEPTIONS=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_CXX_STANDARD=20
            -DCMAKE_CXX_STANDARD_REQUIRED=ON
        RESULT_VARIABLE config_result
        OUTPUT_VARIABLE config_output
        ERROR_VARIABLE config_error
    )

    if(config_result EQUAL 0)
        message(STATUS "✅ spdlog 配置成功")

        # 构建spdlog
        message(STATUS "🔨 正在编译 spdlog...")

        # 获取可用的处理器数量

        # 先输出编译命令以便调试
        message(STATUS "   编译命令: ${CMAKE_COMMAND} --build ${SPDLOG_BUILD_DIR} --config ${SPDLOG_ACTUAL_BUILD_TYPE}")

        # 先检查构建目录是否存在
        if(NOT EXISTS "${SPDLOG_BUILD_DIR}")
            message(FATAL_ERROR "构建目录不存在: ${SPDLOG_BUILD_DIR}")
        endif()

        execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${SPDLOG_BUILD_DIR} --config ${SPDLOG_ACTUAL_BUILD_TYPE}
            RESULT_VARIABLE build_result
            WORKING_DIRECTORY ${SPDLOG_BUILD_DIR}
        )

        if(build_result EQUAL 0)
            message(STATUS "✅ spdlog 编译成功")

            # 安装spdlog
            message(STATUS "📦 正在安装 spdlog...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build ${SPDLOG_BUILD_DIR} --target install --config ${SPDLOG_ACTUAL_BUILD_TYPE}
                RESULT_VARIABLE install_result
                WORKING_DIRECTORY ${SPDLOG_BUILD_DIR}
            )

            if(install_result EQUAL 0)
                message(STATUS "✅ spdlog 安装成功")
                set(spdlog_DIR "${SPDLOG_INSTALL_DIR}/lib/cmake/spdlog" CACHE PATH "spdlog directory")
                find_package(spdlog REQUIRED PATHS ${spdlog_DIR} NO_DEFAULT_PATH)
                set(spdlog_FOUND TRUE)
            else()
                message(FATAL_ERROR "❌ spdlog 安装失败")
            endif()
        else()
            # 输出详细错误信息
            message(STATUS "❌ spdlog 编译失败，返回码: ${build_result}")
            message(FATAL_ERROR "spdlog 编译失败，请查看上面的错误信息")
        endif()
    else()
        message(STATUS "❌ spdlog 配置失败，返回码: ${config_result}")
        if(config_output)
            message(STATUS "配置输出:")
            message(STATUS "${config_output}")
        endif()
        if(config_error)
            message(STATUS "错误信息:")
            message(STATUS "${config_error}")
        endif()
        message(FATAL_ERROR "spdlog 配置失败，请查看上面的错误信息")
    endif()

elseif(SPDLOG_STATUS STREQUAL "ZIP_ONLY")
    # 解压并构建
    message(STATUS "📦 解压 spdlog 源码...")

    # 确保源码目录不存在
    if(EXISTS "${SPDLOG_SOURCE_DIR}")
        message(STATUS "⚠️  清理旧的源码目录...")
        file(REMOVE_RECURSE "${SPDLOG_SOURCE_DIR}")
    endif()

    # 解压到临时目录
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${SPDLOG_ZIP_FILE}
        WORKING_DIRECTORY ${SPDLOG_CACHE_DIR}
        RESULT_VARIABLE extract_result
        OUTPUT_QUIET
        ERROR_VARIABLE extract_error
    )

    if(extract_result EQUAL 0)
        # 检查解压后的目录名（通常是 spdlog-${SPDLOG_VERSION}）
        file(GLOB SPDLOG_EXTRACTED_DIRS "${SPDLOG_CACHE_DIR}/spdlog-*")
        list(GET SPDLOG_EXTRACTED_DIRS 0 SPDLOG_EXTRACTED_DIR)

        if(EXISTS "${SPDLOG_EXTRACTED_DIR}")
            # 移动到标准源码目录
            file(RENAME "${SPDLOG_EXTRACTED_DIR}" "${SPDLOG_SOURCE_DIR}")
            message(STATUS "✅ spdlog 解压成功")

            # 更新状态并重新处理
            set(HAS_SOURCE TRUE)
            set(SPDLOG_STATUS "SOURCE_ONLY")
            message(STATUS "🔄 切换到源码构建模式...")

            # 递归调用处理SOURCE_ONLY状态
            include(${CMAKE_CURRENT_LIST_FILE})
        else()
            message(FATAL_ERROR "❌ 解压后未找到spdlog目录")
        endif()
    else()
        message(WARNING "❌ spdlog 解压失败: ${extract_error}")
        message(STATUS "🔄 删除损坏的压缩包并重新下载...")
        file(REMOVE "${SPDLOG_ZIP_FILE}")
        set(SPDLOG_STATUS "NONE")
        include(${CMAKE_CURRENT_LIST_FILE})
    endif()

else()
    # 需要下载
    message(STATUS "⬇️  开始下载 spdlog ${SPDLOG_VERSION}...")

    # 使用 file(DOWNLOAD) 直接下载，避免创建 subbuild 目录
    set(SPDLOG_DOWNLOAD_URL "https://github.com/gabime/spdlog/archive/refs/tags/v${SPDLOG_VERSION}.tar.gz")

    message(STATUS "📥 正在从 GitHub 下载 spdlog (约3MB)...")
    message(STATUS "   URL: ${SPDLOG_DOWNLOAD_URL}")

    gplatform_download(
        ${SPDLOG_DOWNLOAD_URL}
        ${SPDLOG_ZIP_FILE}
        EXPECTED_HASH SHA256=${SPDLOG_EXPECTED_SHA256}
        STATUS download_status
        LOG download_log
        SHOW_PROGRESS
    )

    list(GET download_status 0 status_code)
    list(GET download_status 1 status_msg)

    if(NOT status_code EQUAL 0)
        message(FATAL_ERROR "❌ spdlog 下载失败: ${status_msg}\n   ${download_log}")
    endif()

    message(STATUS "✅ spdlog 下载成功 (SHA256 校验通过)")

    # 解压
    message(STATUS "📦 解压 spdlog 源码...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${SPDLOG_ZIP_FILE}
        WORKING_DIRECTORY ${SPDLOG_CACHE_DIR}
        RESULT_VARIABLE extract_result
    )

    if(extract_result EQUAL 0)
        message(STATUS "✅ spdlog 下载和解压成功")
        # 递归调用自己来处理ZIP_ONLY状态
        set(SPDLOG_STATUS "ZIP_ONLY")
        include(${CMAKE_CURRENT_LIST_FILE})
    else()
        message(FATAL_ERROR "❌ spdlog 解压失败")
    endif()
endif()

# 设置spdlog可用标志
if(spdlog_FOUND)
    set(SPDLOG_AVAILABLE TRUE CACHE BOOL "spdlog library is available")
    message(STATUS "")
    message(STATUS "🎯 spdlog 库已就绪")
    message(STATUS "   版本: ${SPDLOG_VERSION}")
    message(STATUS "   安装目录: ${SPDLOG_INSTALL_DIR}")

    # 确保spdlog::spdlog目标存在
    if(TARGET spdlog::spdlog)
        message(STATUS "   CMake目标: spdlog::spdlog ✓")
    else()
        message(WARNING "   CMake目标: spdlog::spdlog 未找到")
    endif()
else()
    set(SPDLOG_AVAILABLE FALSE CACHE BOOL "spdlog library is not available")
    message(WARNING "⚠️  spdlog 库不可用")
endif()

# 便利函数：将spdlog库链接到目标
function(link_spdlog_to_target target_name)
    if(SPDLOG_AVAILABLE AND TARGET spdlog::spdlog)
        target_link_libraries(${target_name}
            PRIVATE spdlog::spdlog
        )
        message(STATUS "✅ 已将 spdlog 库链接到目标: ${target_name}")
    else()
        message(WARNING "⚠️  目标 ${target_name} 无法链接 spdlog")
    endif()
endfunction()

message(STATUS "")
message(STATUS "📖 spdlog缓存系统使用说明:")
message(STATUS "   1. 设置环境变量: export CMAKE_FETCH_CACHE=$HOME/.cmake_fetch_cache")
message(STATUS "   2. 首次编译会下载并缓存 spdlog 库")
message(STATUS "   3. 后续编译将直接使用缓存，大幅提升速度")
message(STATUS "   4. 不同的CMAKE_BUILD_TYPE会使用不同的构建目录")
message(STATUS "   5. 安装Ninja可显著加速编译: brew install ninja (macOS) 或 apt install ninja-build (Linux)")
message(STATUS "   6. 清理方法: 删除 ${SPDLOG_CACHE_DIR} 目录")
message(STATUS "")
