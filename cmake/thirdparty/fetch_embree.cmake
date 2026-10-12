cmake_minimum_required(VERSION 3.16)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

# Embree版本配置 - 可以通过CMAKE选项覆盖
if(NOT DEFINED EMBREE_VERSION)
    set(EMBREE_VERSION "4.4.0" CACHE STRING "Embree version to build")
endif()

# Embree 版本与 SHA256 校验表
set(EMBREE_KNOWN_SHA256_4.4.0 "acb517b0ea0f4b442235d5331b69f96192c28da6aca5d5dde0cbe40799638d5c")

if(NOT DEFINED EMBREE_KNOWN_SHA256_${EMBREE_VERSION})
    message(FATAL_ERROR
        "❌ Embree ${EMBREE_VERSION} 未登记 SHA256，请在 fetch_embree.cmake 顶部补充 EMBREE_KNOWN_SHA256_${EMBREE_VERSION}")
endif()
set(EMBREE_EXPECTED_SHA256 "${EMBREE_KNOWN_SHA256_${EMBREE_VERSION}}")

# Embree构建类型配置 - 可以独立于主项目设置
if(DEFINED EMBREE_BUILD_TYPE)
    # 如果明确指定了EMBREE_BUILD_TYPE，使用它
    set(EMBREE_ACTUAL_BUILD_TYPE "${EMBREE_BUILD_TYPE}")
    message(STATUS "   使用指定的Embree构建类型: ${EMBREE_ACTUAL_BUILD_TYPE}")
elseif(CMAKE_BUILD_TYPE)
    # 否则使用主项目的构建类型
    # 如果是 Visual Studio 多配置生成器，只使用 Debug
    if(CMAKE_GENERATOR MATCHES "Visual Studio" AND CMAKE_BUILD_TYPE MATCHES ";")
        set(EMBREE_ACTUAL_BUILD_TYPE "Debug")
        message(STATUS "   检测到 Visual Studio 多配置生成器，使用 Debug 构建 Embree")
    else()
        set(EMBREE_ACTUAL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    endif()
else()
    # 默认Debug
    set(EMBREE_ACTUAL_BUILD_TYPE "Debug")
    message(STATUS "   未指定构建类型，默认使用 Debug")
endif()

gplatform_resolve_fetch_cache(
    EMBREE "embree" "${EMBREE_VERSION}" "${EMBREE_ACTUAL_BUILD_TYPE}")
set(EMBREE_ZIP_FILE "${EMBREE_CACHE_DIR}/embree-${EMBREE_VERSION}.tar.gz")

message(STATUS "")
message(STATUS "🔧 Embree 库缓存管理系统")
message(STATUS "   Embree版本: ${EMBREE_VERSION}")
message(STATUS "   Embree构建类型: ${EMBREE_ACTUAL_BUILD_TYPE}")
if(DEFINED EMBREE_BUILD_TYPE AND NOT EMBREE_BUILD_TYPE STREQUAL CMAKE_BUILD_TYPE)
    message(STATUS "   ⚠️  注意: Embree使用${EMBREE_ACTUAL_BUILD_TYPE}，主项目使用${CMAKE_BUILD_TYPE}")
endif()

# 检测编译工具
find_program(NINJA_EXECUTABLE ninja)
if(NINJA_EXECUTABLE)
    message(STATUS "   编译工具: Ninja (快速编译) ⚡")
else()
    message(STATUS "   编译工具: ${CMAKE_GENERATOR}")
endif()
message(STATUS "   缓存根目录: ${FETCH_CACHE_DIR}")
message(STATUS "   Embree缓存目录: ${EMBREE_CACHE_DIR}")
message(STATUS "   目标架构: ${FETCH_CACHE_ARCH}")
message(STATUS "   构建目录: ${EMBREE_BUILD_DIR}")
message(STATUS "   安装目录: ${EMBREE_INSTALL_DIR}")
message(STATUS "")

# 检查Embree库的状态 - 分别检查各个组件的存在性
set(HAS_INSTALL FALSE)
set(HAS_BUILD FALSE)
set(HAS_SOURCE FALSE)
set(HAS_ZIP FALSE)

set(EMBREE_INSTALL_CONFIG_FILES)
file(GLOB_RECURSE EMBREE_INSTALL_CONFIG_FILES
    "${EMBREE_INSTALL_DIR}/*embree-config.cmake"
    "${EMBREE_INSTALL_DIR}/*EmbreeConfig.cmake")
if(EMBREE_INSTALL_CONFIG_FILES)
    set(HAS_INSTALL TRUE)
    list(GET EMBREE_INSTALL_CONFIG_FILES 0 EMBREE_INSTALL_CONFIG_FILE)
    get_filename_component(EMBREE_INSTALL_CONFIG_DIR "${EMBREE_INSTALL_CONFIG_FILE}" DIRECTORY)
    message(STATUS "✅ 发现安装目录: ${EMBREE_INSTALL_DIR}")
endif()

set(EMBREE_BUILD_CONFIG_FILES)
file(GLOB_RECURSE EMBREE_BUILD_CONFIG_FILES
    "${EMBREE_BUILD_DIR}/*embree-config.cmake"
    "${EMBREE_BUILD_DIR}/*EmbreeConfig.cmake")
if(EMBREE_BUILD_CONFIG_FILES)
    set(HAS_BUILD TRUE)
    set(BUILD_COMPLETE TRUE)
    list(GET EMBREE_BUILD_CONFIG_FILES 0 EMBREE_BUILD_CONFIG_FILE)
    get_filename_component(EMBREE_BUILD_CONFIG_DIR "${EMBREE_BUILD_CONFIG_FILE}" DIRECTORY)
    message(STATUS "✅ 发现已完成的构建目录: ${EMBREE_BUILD_DIR}")
elseif(EXISTS "${EMBREE_BUILD_DIR}/CMakeCache.txt")
    set(HAS_BUILD TRUE)
    set(BUILD_COMPLETE FALSE)
    message(STATUS "📁 发现构建目录（可能未完成）: ${EMBREE_BUILD_DIR}")
endif()

if(EXISTS "${EMBREE_SOURCE_DIR}/CMakeLists.txt")
    set(HAS_SOURCE TRUE)
    message(STATUS "📄 发现源码目录: ${EMBREE_SOURCE_DIR}")
endif()

if(EXISTS "${EMBREE_ZIP_FILE}")
    file(SHA256 "${EMBREE_ZIP_FILE}" EMBREE_ACTUAL_SHA256)
    if(EMBREE_ACTUAL_SHA256 STREQUAL "${EMBREE_EXPECTED_SHA256}")
        set(HAS_ZIP TRUE)
        message(STATUS "📦 发现压缩包: ${EMBREE_ZIP_FILE} (SHA256 校验通过)")
    else()
        message(WARNING "⚠️  缓存压缩包 SHA256 不匹配，将删除并重新下载\n"
            "   期望: ${EMBREE_EXPECTED_SHA256}\n"
            "   实际: ${EMBREE_ACTUAL_SHA256}")
        file(REMOVE "${EMBREE_ZIP_FILE}")
    endif()
endif()

# 决定操作策略
set(EMBREE_STATUS "NONE")
set(embree_FOUND FALSE)

# 优先级判断：install > build > source > zip > none
if(HAS_INSTALL)
    # 有install目录，直接使用
    set(EMBREE_STATUS "INSTALLED")
    set(embree_FOUND TRUE)
    message(STATUS "🚀 将使用已安装的Embree库")

elseif(HAS_BUILD AND NOT HAS_INSTALL)
    # 检查构建是否完成
    if(BUILD_COMPLETE)
        # 构建已完成，可以直接使用
        set(EMBREE_STATUS "BUILT_COMPLETE")
        set(embree_FOUND TRUE)
        message(STATUS "✅ 将直接使用已编译的Embree（无需安装）")
    else()
        # 构建未完成，需要继续
        set(EMBREE_STATUS "BUILT_NOT_INSTALLED")
        message(STATUS "⚠️  已构建但未完成，将继续编译")
    endif()

elseif(HAS_SOURCE AND NOT HAS_BUILD)
    # 有源码但没有build，需要构建
    set(EMBREE_STATUS "SOURCE_ONLY")
    message(STATUS "🔨 有源码无构建，将进行构建和安装")

elseif(HAS_ZIP AND NOT HAS_SOURCE)
    # 只有zip，需要解压
    set(EMBREE_STATUS "ZIP_ONLY")
    message(STATUS "📦 只有压缩包，将解压并构建")

else()
    # 什么都没有，需要下载
    set(EMBREE_STATUS "NONE")
    message(STATUS "⬇️  无缓存文件，将下载Embree")
endif()

message(STATUS "📊 状态汇总: Install=${HAS_INSTALL}, Build=${HAS_BUILD}, Source=${HAS_SOURCE}, Zip=${HAS_ZIP}")
message(STATUS "🎯 执行策略: ${EMBREE_STATUS}")

# 根据状态执行相应操作
if(EMBREE_STATUS STREQUAL "INSTALLED")
    # 直接使用已安装的Embree
    message(STATUS "🚀 使用缓存的 Embree 库，跳过下载和编译")
    set(embree_DIR "${EMBREE_INSTALL_CONFIG_DIR}" CACHE PATH "Embree directory")
    find_package(embree ${EMBREE_VERSION} REQUIRED CONFIG PATHS ${embree_DIR} NO_DEFAULT_PATH)

elseif(EMBREE_STATUS STREQUAL "BUILT_COMPLETE")
    # 构建已完成，执行安装步骤
    message(STATUS "📦 开始安装 Embree...")

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${EMBREE_BUILD_DIR} --target install --config ${EMBREE_ACTUAL_BUILD_TYPE}
        RESULT_VARIABLE install_result
        WORKING_DIRECTORY ${EMBREE_BUILD_DIR}
    )

    if(install_result EQUAL 0)
        message(STATUS "✅ Embree 安装成功")
        set(embree_DIR "${EMBREE_INSTALL_CONFIG_DIR}" CACHE PATH "Embree directory")
        find_package(embree ${EMBREE_VERSION} REQUIRED CONFIG PATHS ${embree_DIR} NO_DEFAULT_PATH)
        set(embree_FOUND TRUE)
    else()
        message(FATAL_ERROR "❌ Embree 安装失败")
    endif()

elseif(EMBREE_STATUS STREQUAL "BUILT_NOT_INSTALLED")
    # 执行安装
    message(STATUS "📦 开始安装 Embree...")

    if(EXISTS "${EMBREE_BUILD_DIR}/CMakeCache.txt")
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${EMBREE_BUILD_DIR} --target install --config ${EMBREE_ACTUAL_BUILD_TYPE}
            RESULT_VARIABLE install_result
            WORKING_DIRECTORY ${EMBREE_BUILD_DIR}
        )

        if(install_result EQUAL 0)
            message(STATUS "✅ Embree 安装成功")
            file(GLOB_RECURSE EMBREE_INSTALL_CONFIG_FILES
                "${EMBREE_INSTALL_DIR}/*embree-config.cmake"
                "${EMBREE_INSTALL_DIR}/*EmbreeConfig.cmake")
            if(EMBREE_INSTALL_CONFIG_FILES)
                list(GET EMBREE_INSTALL_CONFIG_FILES 0 EMBREE_INSTALL_CONFIG_FILE)
                get_filename_component(EMBREE_INSTALL_CONFIG_DIR "${EMBREE_INSTALL_CONFIG_FILE}" DIRECTORY)
            endif()
            set(embree_DIR "${EMBREE_INSTALL_CONFIG_DIR}" CACHE PATH "Embree directory")
            find_package(embree ${EMBREE_VERSION} REQUIRED CONFIG PATHS ${embree_DIR} NO_DEFAULT_PATH)
            set(embree_FOUND TRUE)
        else()
            message(WARNING "❌ Embree 安装失败，尝试重新构建...")
            file(REMOVE_RECURSE "${EMBREE_BUILD_DIR}")
            set(EMBREE_STATUS "SOURCE_ONLY")
            include(${CMAKE_CURRENT_LIST_FILE})
        endif()
    else()
        message(WARNING "⚠️  构建目录不完整，重新构建...")
        file(REMOVE_RECURSE "${EMBREE_BUILD_DIR}")
        set(EMBREE_STATUS "SOURCE_ONLY")
        include(${CMAKE_CURRENT_LIST_FILE})
    endif()

elseif(EMBREE_STATUS STREQUAL "SOURCE_ONLY")
    # 需要构建和安装
    message(STATUS "🔨 开始构建 Embree...")

    # 检测可用的生成器
    set(EMBREE_GENERATOR "")
    set(EMBREE_GENERATOR_PLATFORM "")
    set(EMBREE_MAKE_PROGRAM "")

    if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio")
        set(EMBREE_GENERATOR "${CMAKE_GENERATOR}")
        if(CMAKE_GENERATOR_PLATFORM)
            set(EMBREE_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
        endif()
        message(STATUS "⚡ 使用项目生成器: ${EMBREE_GENERATOR}")
        if(EMBREE_GENERATOR_PLATFORM)
            message(STATUS "   平台: ${EMBREE_GENERATOR_PLATFORM}")
        endif()
    else()
        find_program(NINJA_EXECUTABLE ninja)
        if(NINJA_EXECUTABLE)
            set(EMBREE_GENERATOR "Ninja")
            set(EMBREE_MAKE_PROGRAM ${NINJA_EXECUTABLE})
            message(STATUS "🚀 使用 Ninja 生成器进行快速编译")
            message(STATUS "   Ninja路径: ${NINJA_EXECUTABLE}")
        else()
            if(CMAKE_GENERATOR)
                set(EMBREE_GENERATOR "${CMAKE_GENERATOR}")
                message(STATUS "⚡ 使用项目生成器: ${CMAKE_GENERATOR}")
            else()
                set(EMBREE_GENERATOR "Unix Makefiles")
                message(STATUS "🔧 使用默认生成器: Unix Makefiles")
            endif()

            if(CMAKE_MAKE_PROGRAM)
                set(EMBREE_MAKE_PROGRAM "${CMAKE_MAKE_PROGRAM}")
            endif()
        endif()
    endif()

    # 准备生成器参数 - 注意：-G 和生成器名称必须分开
    set(GENERATOR_ARGS -G "${EMBREE_GENERATOR}")
    if(EMBREE_GENERATOR_PLATFORM)
        list(APPEND GENERATOR_ARGS -A ${EMBREE_GENERATOR_PLATFORM})
    endif()
    if(EMBREE_MAKE_PROGRAM)
        list(APPEND GENERATOR_ARGS -DCMAKE_MAKE_PROGRAM=${EMBREE_MAKE_PROGRAM})
    endif()

    # 使用 Ninja 时需要显式指定编译器路径
    if(EMBREE_GENERATOR STREQUAL "Ninja")
        if(CMAKE_C_COMPILER)
            list(APPEND GENERATOR_ARGS -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
        endif()
        if(CMAKE_CXX_COMPILER)
            list(APPEND GENERATOR_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
        endif()
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S ${EMBREE_SOURCE_DIR}
            -B ${EMBREE_BUILD_DIR}
            ${GENERATOR_ARGS}
            ${FETCH_CACHE_FORWARD_ARGS}
            -DCMAKE_BUILD_TYPE=${EMBREE_ACTUAL_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${EMBREE_INSTALL_DIR}
            -DEMBREE_ISPC_SUPPORT=OFF
            -DEMBREE_TUTORIALS=OFF
            -DEMBREE_TASKING_SYSTEM=INTERNAL
            -DEMBREE_STATIC_LIB=OFF
            -DEMBREE_TESTING=OFF
            -DBUILD_SHARED_LIBS=ON
            -DCMAKE_CXX_STANDARD=17
            -DCMAKE_CXX_STANDARD_REQUIRED=ON
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        RESULT_VARIABLE config_result
        OUTPUT_VARIABLE config_output
        ERROR_VARIABLE config_error
    )

    if(config_result EQUAL 0)
        message(STATUS "✅ Embree 配置成功")

        # 构建Embree
        message(STATUS "🔨 正在编译 Embree...")


        message(STATUS "   编译命令: ${CMAKE_COMMAND} --build ${EMBREE_BUILD_DIR} --config ${EMBREE_ACTUAL_BUILD_TYPE}")

        if(NOT EXISTS "${EMBREE_BUILD_DIR}")
            message(FATAL_ERROR "构建目录不存在: ${EMBREE_BUILD_DIR}")
        endif()

        execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${EMBREE_BUILD_DIR} --config ${EMBREE_ACTUAL_BUILD_TYPE}
            RESULT_VARIABLE build_result
            WORKING_DIRECTORY ${EMBREE_BUILD_DIR}
        )

        if(build_result EQUAL 0)
            message(STATUS "✅ Embree 编译成功")

            # 安装Embree
            message(STATUS "📦 正在安装 Embree...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build ${EMBREE_BUILD_DIR} --target install --config ${EMBREE_ACTUAL_BUILD_TYPE}
                RESULT_VARIABLE install_result
                WORKING_DIRECTORY ${EMBREE_BUILD_DIR}
            )

            if(install_result EQUAL 0)
                message(STATUS "✅ Embree 安装成功")
                file(GLOB_RECURSE EMBREE_INSTALL_CONFIG_FILES
                    "${EMBREE_INSTALL_DIR}/*embree-config.cmake"
                    "${EMBREE_INSTALL_DIR}/*EmbreeConfig.cmake")
                if(EMBREE_INSTALL_CONFIG_FILES)
                    list(GET EMBREE_INSTALL_CONFIG_FILES 0 EMBREE_INSTALL_CONFIG_FILE)
                    get_filename_component(EMBREE_INSTALL_CONFIG_DIR "${EMBREE_INSTALL_CONFIG_FILE}" DIRECTORY)
                endif()
                set(embree_DIR "${EMBREE_INSTALL_CONFIG_DIR}" CACHE PATH "Embree directory")
                find_package(embree ${EMBREE_VERSION} REQUIRED CONFIG PATHS ${embree_DIR} NO_DEFAULT_PATH)
                set(embree_FOUND TRUE)
            else()
                message(FATAL_ERROR "❌ Embree 安装失败")
            endif()
        else()
            message(STATUS "❌ Embree 编译失败，返回码: ${build_result}")
            if(build_output)
                message(STATUS "编译输出（最后1000字符）:")
                string(LENGTH "${build_output}" output_len)
                if(output_len GREATER 1000)
                    math(EXPR build_output_start "${output_len} - 1000")
                    string(SUBSTRING "${build_output}" ${build_output_start} 1000 build_output_tail)
                    message(STATUS "${build_output_tail}")
                else()
                    message(STATUS "${build_output}")
                endif()
            endif()

            if(build_error)
                message(STATUS "错误信息:")
                message(STATUS "${build_error}")
            endif()

            file(GLOB error_logs "${EMBREE_BUILD_DIR}/CMakeFiles/*.log")
            if(error_logs)
                message(STATUS "找到以下日志文件:")
                foreach(log ${error_logs})
                    message(STATUS "  ${log}")
                endforeach()
            endif()

            message(FATAL_ERROR "Embree 编译失败，请查看上面的错误信息")
        endif()
    else()
        message(STATUS "❌ Embree 配置失败，返回码: ${config_result}")
        if(config_output)
            message(STATUS "配置输出:")
            message(STATUS "${config_output}")
        endif()
        if(config_error)
            message(STATUS "错误信息:")
            message(STATUS "${config_error}")
        endif()
        message(FATAL_ERROR "Embree 配置失败，请查看上面的错误信息")
    endif()

elseif(EMBREE_STATUS STREQUAL "ZIP_ONLY")
    # 解压并构建
    message(STATUS "📦 解压 Embree 源码...")

    if(EXISTS "${EMBREE_SOURCE_DIR}")
        message(STATUS "⚠️  清理旧的源码目录...")
        file(REMOVE_RECURSE "${EMBREE_SOURCE_DIR}")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${EMBREE_ZIP_FILE}
        WORKING_DIRECTORY ${EMBREE_CACHE_DIR}
        RESULT_VARIABLE extract_result
        OUTPUT_QUIET
        ERROR_VARIABLE extract_error
    )

    if(extract_result EQUAL 0)
        file(GLOB EMBREE_EXTRACTED_DIRS "${EMBREE_CACHE_DIR}/embree-*")
        list(FILTER EMBREE_EXTRACTED_DIRS EXCLUDE REGEX ".*/build-.*")
        list(FILTER EMBREE_EXTRACTED_DIRS EXCLUDE REGEX ".*/install-.*")
        if(EMBREE_EXTRACTED_DIRS)
            list(GET EMBREE_EXTRACTED_DIRS 0 EMBREE_EXTRACTED_DIR)
        endif()

        if(EXISTS "${EMBREE_EXTRACTED_DIR}")
            file(RENAME "${EMBREE_EXTRACTED_DIR}" "${EMBREE_SOURCE_DIR}")
            message(STATUS "✅ Embree 解压成功")

            set(HAS_SOURCE TRUE)
            set(EMBREE_STATUS "SOURCE_ONLY")
            message(STATUS "🔄 切换到源码构建模式...")
            include(${CMAKE_CURRENT_LIST_FILE})
        else()
            message(FATAL_ERROR "❌ 解压后未找到Embree目录")
        endif()
    else()
        message(WARNING "❌ Embree 解压失败: ${extract_error}")
        message(STATUS "🔄 删除损坏的压缩包并重新下载...")
        file(REMOVE "${EMBREE_ZIP_FILE}")
        set(EMBREE_STATUS "NONE")
        include(${CMAKE_CURRENT_LIST_FILE})
    endif()

else()
    # 需要下载
    message(STATUS "⬇️  开始下载 Embree ${EMBREE_VERSION}...")

    set(EMBREE_DOWNLOAD_URL "https://github.com/RenderKit/embree/archive/refs/tags/v${EMBREE_VERSION}.tar.gz")

    message(STATUS "📥 正在下载 Embree ...")
    message(STATUS "   URL: ${EMBREE_DOWNLOAD_URL}")

    file(DOWNLOAD
        ${EMBREE_DOWNLOAD_URL}
        ${EMBREE_ZIP_FILE}
        EXPECTED_HASH SHA256=${EMBREE_EXPECTED_SHA256}
        STATUS download_status
        LOG download_log
        SHOW_PROGRESS
    )

    list(GET download_status 0 status_code)
    list(GET download_status 1 status_msg)

    if(NOT status_code EQUAL 0)
        message(FATAL_ERROR "❌ Embree 下载失败: ${status_msg}\n   ${download_log}")
    endif()

    message(STATUS "✅ Embree 下载成功 (SHA256 校验通过)")

    message(STATUS "📦 解压 Embree 源码...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${EMBREE_ZIP_FILE}
        WORKING_DIRECTORY ${EMBREE_CACHE_DIR}
        RESULT_VARIABLE extract_result
    )

    if(extract_result EQUAL 0)
        file(GLOB EMBREE_EXTRACTED_DIRS "${EMBREE_CACHE_DIR}/embree-*")
        list(FILTER EMBREE_EXTRACTED_DIRS EXCLUDE REGEX ".*/build-.*")
        list(FILTER EMBREE_EXTRACTED_DIRS EXCLUDE REGEX ".*/install-.*")
        if(EMBREE_EXTRACTED_DIRS)
            list(GET EMBREE_EXTRACTED_DIRS 0 EMBREE_EXTRACTED_DIR)
        endif()

        if(EXISTS "${EMBREE_EXTRACTED_DIR}")
            file(RENAME "${EMBREE_EXTRACTED_DIR}" "${EMBREE_SOURCE_DIR}")
        endif()

        message(STATUS "✅ Embree 下载和解压成功")
        set(EMBREE_STATUS "SOURCE_ONLY")
        include(${CMAKE_CURRENT_LIST_FILE})
    else()
        message(FATAL_ERROR "❌ Embree 解压失败")
    endif()
endif()

if(embree_FOUND)
    if(TARGET embree)
        set(EMBREE_IMPORTED_TARGET embree CACHE INTERNAL "Embree imported target")
    elseif(TARGET embree::embree)
        set(EMBREE_IMPORTED_TARGET embree::embree CACHE INTERNAL "Embree imported target")
    else()
        set(EMBREE_IMPORTED_TARGET "" CACHE INTERNAL "Embree imported target")
    endif()

    set(EMBREE_AVAILABLE TRUE CACHE BOOL "Embree library is available")
    message(STATUS "")
    message(STATUS "🎯 Embree 库已就绪")
    message(STATUS "   版本: ${EMBREE_VERSION}")
    message(STATUS "   安装目录: ${EMBREE_INSTALL_DIR}")
    if(EMBREE_IMPORTED_TARGET)
        message(STATUS "   导入目标: ${EMBREE_IMPORTED_TARGET}")
    endif()
else()
    set(EMBREE_AVAILABLE FALSE CACHE BOOL "Embree library is not available")
    message(WARNING "⚠️  Embree 库不可用")
endif()

function(link_embree_to_target target_name)
    if(EMBREE_AVAILABLE)
        if(EMBREE_IMPORTED_TARGET)
            target_link_libraries(${target_name}
                PRIVATE ${EMBREE_IMPORTED_TARGET}
            )
        elseif(embree_LIBRARIES)
            target_link_libraries(${target_name}
                PRIVATE ${embree_LIBRARIES}
            )
        endif()

        if(embree_INCLUDE_DIRS)
            target_include_directories(${target_name}
                PRIVATE ${embree_INCLUDE_DIRS}
            )
        endif()

        target_compile_definitions(${target_name}
            PRIVATE GPLATFORM_HAS_EMBREE=1
        )

        message(STATUS "✅ 已将 Embree 库链接到目标: ${target_name}")
    else()
        message(WARNING "⚠️  目标 ${target_name} 无法链接 Embree")
    endif()
endfunction()

message(STATUS "")
message(STATUS "📋 Embree 缓存管理说明:")
message(STATUS "   1. 设置环境变量: export CMAKE_FETCH_CACHE=$HOME/.cmake_fetch_cache")
message(STATUS "   2. Embree将缓存在: ${EMBREE_CACHE_DIR}")
message(STATUS "   3. 不同构建类型使用独立目录，避免冲突")
message(STATUS "   4. 下次配置时将自动复用缓存，跳过重复编译")
message(STATUS "")
