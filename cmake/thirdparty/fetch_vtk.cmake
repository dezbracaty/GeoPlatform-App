cmake_minimum_required(VERSION 3.16)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

# VTK版本配置 - 可以通过CMAKE选项覆盖
if(NOT DEFINED VTK_VERSION)
    set(VTK_VERSION "9.5.0" CACHE STRING "VTK version to build")
endif()

# 自动计算主版本号
string(REGEX MATCH "^[0-9]+\\.[0-9]+" VTK_VERSION_MAJOR "${VTK_VERSION}")

# VTK 版本与 SHA256 校验表
set(VTK_KNOWN_SHA256_9.5.0 "04ae86246b9557c6b61afbc534a6df099244fbc8f3937f82e6bc0570953af87d")

if(NOT DEFINED VTK_KNOWN_SHA256_${VTK_VERSION})
    message(FATAL_ERROR
        "❌ VTK ${VTK_VERSION} 未登记 SHA256，请在 fetch_vtk.cmake 顶部补充 VTK_KNOWN_SHA256_${VTK_VERSION}")
endif()
set(VTK_EXPECTED_SHA256 "${VTK_KNOWN_SHA256_${VTK_VERSION}}")

# VTK构建类型配置 - 可以独立于主项目设置
if(DEFINED VTK_BUILD_TYPE)
    # 如果明确指定了VTK_BUILD_TYPE，使用它
    set(VTK_ACTUAL_BUILD_TYPE "${VTK_BUILD_TYPE}")
    message(STATUS "   使用指定的VTK构建类型: ${VTK_ACTUAL_BUILD_TYPE}")
elseif(CMAKE_BUILD_TYPE)
    # 否则使用主项目的构建类型
    # 如果是 Visual Studio 多配置生成器，只使用 Debug
    if(CMAKE_GENERATOR MATCHES "Visual Studio" AND CMAKE_BUILD_TYPE MATCHES ";")
        set(VTK_ACTUAL_BUILD_TYPE "Debug")
        message(STATUS "   检测到 Visual Studio 多配置生成器，使用 Debug 构建 VTK")
    else()
        set(VTK_ACTUAL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    endif()
else()
    # 默认Debug
    set(VTK_ACTUAL_BUILD_TYPE "Debug")
    message(STATUS "   未指定构建类型，默认使用 Debug")
endif()

gplatform_resolve_fetch_cache(
    VTK "vtk" "${VTK_VERSION}" "${VTK_ACTUAL_BUILD_TYPE}")
set(VTK_ZIP_FILE "${VTK_CACHE_DIR}/VTK-${VTK_VERSION}.tar.gz")

message(STATUS "")
message(STATUS "🔧 VTK 库缓存管理系统")
message(STATUS "   VTK版本: ${VTK_VERSION}")
message(STATUS "   VTK构建类型: ${VTK_ACTUAL_BUILD_TYPE}")
if(DEFINED VTK_BUILD_TYPE AND NOT VTK_BUILD_TYPE STREQUAL CMAKE_BUILD_TYPE)
    message(STATUS "   ⚠️  注意: VTK使用${VTK_ACTUAL_BUILD_TYPE}，主项目使用${CMAKE_BUILD_TYPE}")
endif()

# 检测编译工具
find_program(NINJA_EXECUTABLE ninja)
if(NINJA_EXECUTABLE)
    message(STATUS "   编译工具: Ninja (快速编译) ⚡")
else()
    message(STATUS "   编译工具: ${CMAKE_GENERATOR}")
endif()
message(STATUS "   缓存根目录: ${FETCH_CACHE_DIR}")
message(STATUS "   VTK缓存目录: ${VTK_CACHE_DIR}")
message(STATUS "   目标架构: ${FETCH_CACHE_ARCH}")
message(STATUS "   构建目录: ${VTK_BUILD_DIR}")
message(STATUS "   安装目录: ${VTK_INSTALL_DIR}")

# 显示Qt信息
if(Qt6_FOUND)
    message(STATUS "   Qt6状态: ✅ 已找到")
    message(STATUS "   Qt6路径: ${Qt6_DIR}")
else()
    message(STATUS "   Qt6状态: ❌ 未找到 (VTK将不包含Qt支持)")
endif()

message(STATUS "")

# 检查VTK库的状态 - 分别检查各个组件的存在性
set(HAS_INSTALL FALSE)
set(HAS_BUILD FALSE)
set(HAS_SOURCE FALSE)
set(HAS_ZIP FALSE)

# 检查各个组件
# VTK 9.3+ 使用小写的 vtk-config.cmake
if(EXISTS "${VTK_INSTALL_DIR}/lib/cmake/vtk-${VTK_VERSION_MAJOR}/vtk-config.cmake" OR
   EXISTS "${VTK_INSTALL_DIR}/lib/cmake/vtk-${VTK_VERSION_MAJOR}/VTKConfig.cmake")
    set(HAS_INSTALL TRUE)
    message(STATUS "✅ 发现安装目录: ${VTK_INSTALL_DIR}")
endif()

# 检查构建目录 - 检查多种可能的配置文件位置
if(EXISTS "${VTK_BUILD_DIR}/lib/cmake/vtk-${VTK_VERSION_MAJOR}/vtk-config.cmake" OR
   EXISTS "${VTK_BUILD_DIR}/lib/cmake/vtk-${VTK_VERSION_MAJOR}/VTKConfig.cmake" OR
   EXISTS "${VTK_BUILD_DIR}/VTKConfig.cmake")
    set(HAS_BUILD TRUE)
    set(BUILD_COMPLETE TRUE)
    message(STATUS "✅ 发现已完成的构建目录: ${VTK_BUILD_DIR}")
elseif(EXISTS "${VTK_BUILD_DIR}/CMakeCache.txt")
    set(HAS_BUILD TRUE)
    set(BUILD_COMPLETE FALSE)
    message(STATUS "📁 发现构建目录（可能未完成）: ${VTK_BUILD_DIR}")
endif()

if(EXISTS "${VTK_SOURCE_DIR}/CMakeLists.txt")
    set(HAS_SOURCE TRUE)
    message(STATUS "📄 发现源码目录: ${VTK_SOURCE_DIR}")
endif()

if(EXISTS "${VTK_ZIP_FILE}")
    file(SHA256 "${VTK_ZIP_FILE}" VTK_ACTUAL_SHA256)
    if(VTK_ACTUAL_SHA256 STREQUAL "${VTK_EXPECTED_SHA256}")
        set(HAS_ZIP TRUE)
        message(STATUS "📦 发现压缩包: ${VTK_ZIP_FILE} (SHA256 校验通过)")
    else()
        message(WARNING "⚠️  缓存压缩包 SHA256 不匹配，将删除并重新下载\n"
            "   期望: ${VTK_EXPECTED_SHA256}\n"
            "   实际: ${VTK_ACTUAL_SHA256}")
        file(REMOVE "${VTK_ZIP_FILE}")
    endif()
endif()

# 决定操作策略
set(VTK_STATUS "NONE")
set(VTK_FOUND FALSE)

# 优先级判断：install > build > source > zip > none
if(HAS_INSTALL)
    # 有install目录，直接使用
    set(VTK_STATUS "INSTALLED")
    set(VTK_FOUND TRUE)
    message(STATUS "🚀 将使用已安装的VTK库")

elseif(HAS_BUILD AND NOT HAS_INSTALL)
    # 检查构建是否完成
    if(BUILD_COMPLETE)
        # 构建已完成，可以直接使用
        set(VTK_STATUS "BUILT_COMPLETE")
        set(VTK_FOUND TRUE)
        message(STATUS "✅ 将直接使用已编译的VTK（无需安装）")
    else()
        # 构建未完成，需要继续
        set(VTK_STATUS "BUILT_NOT_INSTALLED")
        message(STATUS "⚠️  已构建但未完成，将继续编译")
    endif()

elseif(HAS_SOURCE AND NOT HAS_BUILD)
    # 有源码但没有build，需要构建
    set(VTK_STATUS "SOURCE_ONLY")
    message(STATUS "🔨 有源码无构建，将进行构建和安装")

elseif(HAS_ZIP AND NOT HAS_SOURCE)
    # 只有zip，需要解压
    set(VTK_STATUS "ZIP_ONLY")
    message(STATUS "📦 只有压缩包，将解压并构建")

else()
    # 什么都没有，需要下载
    set(VTK_STATUS "NONE")
    message(STATUS "⬇️  无缓存文件，将下载VTK")
endif()

message(STATUS "📊 状态汇总: Install=${HAS_INSTALL}, Build=${HAS_BUILD}, Source=${HAS_SOURCE}, Zip=${HAS_ZIP}")
message(STATUS "🎯 执行策略: ${VTK_STATUS}")

# 根据状态执行相应操作
if(VTK_STATUS STREQUAL "INSTALLED")
    # 直接使用已安装的VTK
    message(STATUS "🚀 使用缓存的 VTK 库，跳过下载和编译")
    set(VTK_DIR "${VTK_INSTALL_DIR}/lib/cmake/vtk-${VTK_VERSION_MAJOR}" CACHE PATH "VTK directory")
    find_package(VTK ${VTK_VERSION} REQUIRED PATHS ${VTK_DIR})

elseif(VTK_STATUS STREQUAL "BUILT_COMPLETE")
    # 构建已完成，执行安装步骤
    message(STATUS "📦 开始安装 VTK...")

    # 执行安装
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${VTK_BUILD_DIR} --target install --config ${VTK_ACTUAL_BUILD_TYPE}
        RESULT_VARIABLE install_result
        WORKING_DIRECTORY ${VTK_BUILD_DIR}
    )

    if(install_result EQUAL 0)
        message(STATUS "✅ VTK 安装成功")
        set(VTK_DIR "${VTK_INSTALL_DIR}/lib/cmake/vtk-${VTK_VERSION_MAJOR}" CACHE PATH "VTK directory")
        find_package(VTK ${VTK_VERSION} REQUIRED PATHS ${VTK_DIR})
        set(VTK_FOUND TRUE)
    else()
        message(FATAL_ERROR "❌ VTK 安装失败")
    endif()

elseif(VTK_STATUS STREQUAL "BUILT_NOT_INSTALLED")
    # 执行安装
    message(STATUS "📦 开始安装 VTK...")

    # 先检查构建是否完整
    if(EXISTS "${VTK_BUILD_DIR}/lib")
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${VTK_BUILD_DIR} --target install --config ${VTK_ACTUAL_BUILD_TYPE}
            RESULT_VARIABLE install_result
            WORKING_DIRECTORY ${VTK_BUILD_DIR}
        )

        if(install_result EQUAL 0)
            message(STATUS "✅ VTK 安装成功")
            set(VTK_DIR "${VTK_INSTALL_DIR}/lib/cmake/vtk-${VTK_VERSION_MAJOR}" CACHE PATH "VTK directory")
            find_package(VTK ${VTK_VERSION} REQUIRED PATHS ${VTK_DIR})
            set(VTK_FOUND TRUE)
        else()
            message(WARNING "❌ VTK 安装失败，尝试重新构建...")
            # 删除不完整的构建并重新构建
            file(REMOVE_RECURSE "${VTK_BUILD_DIR}")
            set(VTK_STATUS "SOURCE_ONLY")
            include(${CMAKE_CURRENT_LIST_FILE})
        endif()
    else()
        message(WARNING "⚠️  构建目录不完整，重新构建...")
        file(REMOVE_RECURSE "${VTK_BUILD_DIR}")
        set(VTK_STATUS "SOURCE_ONLY")
        include(${CMAKE_CURRENT_LIST_FILE})
    endif()

elseif(VTK_STATUS STREQUAL "SOURCE_ONLY")
    # 需要构建和安装
    message(STATUS "🔨 开始构建 VTK...")

    # 检查Qt6是否可用
    set(VTK_QT_ARGS "")
    if(Qt6_FOUND)
        message(STATUS "   检测到Qt6，启用Qt支持")
        message(STATUS "   Qt6_DIR: ${Qt6_DIR}")

        # 添加Qt相关配置
        list(APPEND VTK_QT_ARGS
            -DQt6_DIR=${Qt6_DIR}
            -DVTK_GROUP_ENABLE_Qt=YES
            -DVTK_MODULE_ENABLE_VTK_GUISupportQt=YES
            -DVTK_MODULE_ENABLE_VTK_GUISupportQtQuick=NO
            -DVTK_MODULE_ENABLE_VTK_RenderingQt=YES
            -DVTK_MODULE_ENABLE_VTK_ViewsQt=YES
        )

        # 如果找到了Qt的其他组件，也传递它们
        if(Qt6Core_DIR)
            list(APPEND VTK_QT_ARGS -DQt6Core_DIR=${Qt6Core_DIR})
        endif()
        if(Qt6Widgets_DIR)
            list(APPEND VTK_QT_ARGS -DQt6Widgets_DIR=${Qt6Widgets_DIR})
        endif()
        if(Qt6OpenGL_DIR)
            list(APPEND VTK_QT_ARGS -DQt6OpenGL_DIR=${Qt6OpenGL_DIR})
        endif()
        if(Qt6Quick_DIR)
            list(APPEND VTK_QT_ARGS -DQt6Quick_DIR=${Qt6Quick_DIR})
        endif()
        if(Qt6Qml_DIR)
            list(APPEND VTK_QT_ARGS -DQt6Qml_DIR=${Qt6Qml_DIR})
        endif()
    else()
        message(WARNING "   Qt6未找到，VTK将不包含Qt支持")
        list(APPEND VTK_QT_ARGS
            -DVTK_GROUP_ENABLE_Qt=NO
        )
    endif()

    # 检测可用的生成器
    set(VTK_GENERATOR "")
    set(VTK_GENERATOR_PLATFORM "")
    set(VTK_MAKE_PROGRAM "")

    # 在 Windows 上，优先使用与主项目相同的生成器
    if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio")
        set(VTK_GENERATOR "${CMAKE_GENERATOR}")
        if(CMAKE_GENERATOR_PLATFORM)
            set(VTK_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
        endif()
        message(STATUS "⚡ 使用项目生成器: ${VTK_GENERATOR}")
        if(VTK_GENERATOR_PLATFORM)
            message(STATUS "   平台: ${VTK_GENERATOR_PLATFORM}")
        endif()
    else()
        # 在非 Windows 平台上，优先使用 Ninja
        find_program(NINJA_EXECUTABLE ninja)
        if(NINJA_EXECUTABLE)
            set(VTK_GENERATOR "Ninja")
            set(VTK_MAKE_PROGRAM ${NINJA_EXECUTABLE})
            message(STATUS "🚀 使用 Ninja 生成器进行快速编译")
            message(STATUS "   Ninja路径: ${NINJA_EXECUTABLE}")
        else()
            # 如果没有 Ninja，使用与主项目相同的生成器
            if(CMAKE_GENERATOR)
                set(VTK_GENERATOR "${CMAKE_GENERATOR}")
                message(STATUS "⚡ 使用项目生成器: ${CMAKE_GENERATOR}")
            else()
                # 默认使用 Unix Makefiles
                set(VTK_GENERATOR "Unix Makefiles")
                message(STATUS "🔧 使用默认生成器: Unix Makefiles")
            endif()

            if(CMAKE_MAKE_PROGRAM)
                set(VTK_MAKE_PROGRAM "${CMAKE_MAKE_PROGRAM}")
            endif()
        endif()
    endif()

    # 准备生成器参数 - 注意：-G 和生成器名称必须分开
    set(GENERATOR_ARGS -G "${VTK_GENERATOR}")
    if(VTK_GENERATOR_PLATFORM)
        list(APPEND GENERATOR_ARGS -A ${VTK_GENERATOR_PLATFORM})
    endif()
    if(VTK_MAKE_PROGRAM)
        list(APPEND GENERATOR_ARGS -DCMAKE_MAKE_PROGRAM=${VTK_MAKE_PROGRAM})
    endif()

    # 使用 Ninja 时需要显式指定编译器路径
    if(VTK_GENERATOR STREQUAL "Ninja")
        if(CMAKE_C_COMPILER)
            list(APPEND GENERATOR_ARGS -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
        endif()
        if(CMAKE_CXX_COMPILER)
            list(APPEND GENERATOR_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
        endif()
    endif()

    # Windows does not provide a system zlib. VTK's bundled copy is isolated
    # from the slicer's dependencies; preserve the existing Unix system-zlib use.
    if(WIN32)
        set(VTK_EXTERNAL_ZLIB OFF)
    else()
        set(VTK_EXTERNAL_ZLIB ON)
    endif()

    # 配置VTK
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S ${VTK_SOURCE_DIR}
            -B ${VTK_BUILD_DIR}
            ${GENERATOR_ARGS}
            ${FETCH_CACHE_FORWARD_ARGS}
            -DCMAKE_BUILD_TYPE=${VTK_ACTUAL_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${VTK_INSTALL_DIR}
            ${VTK_QT_ARGS}
            -DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=YES
            -DVTK_GROUP_ENABLE_Rendering=YES
            -DVTK_GROUP_ENABLE_StandAlone=YES
            -DVTK_BUILD_TESTING=OFF
            -DVTK_BUILD_EXAMPLES=OFF
            -DVTK_BUILD_DOCUMENTATION=OFF
            -DBUILD_SHARED_LIBS=ON
            -DVTK_MODULE_USE_EXTERNAL_VTK_zlib=${VTK_EXTERNAL_ZLIB}
            -DCMAKE_CXX_STANDARD=20
            -DCMAKE_CXX_STANDARD_REQUIRED=ON
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        RESULT_VARIABLE config_result
        OUTPUT_VARIABLE config_output
        ERROR_VARIABLE config_error
    )

    if(config_result EQUAL 0)
        message(STATUS "✅ VTK 配置成功")

        # 构建VTK
        message(STATUS "🔨 正在编译 VTK (这可能需要较长时间)...")

        # 获取可用的处理器数量

        # 先输出编译命令以便调试
        message(STATUS "   编译命令: ${CMAKE_COMMAND} --build ${VTK_BUILD_DIR} --config ${VTK_ACTUAL_BUILD_TYPE}")

        # 先检查构建目录是否存在
        if(NOT EXISTS "${VTK_BUILD_DIR}")
            message(FATAL_ERROR "构建目录不存在: ${VTK_BUILD_DIR}")
        endif()

        execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${VTK_BUILD_DIR} --config ${VTK_ACTUAL_BUILD_TYPE}
            RESULT_VARIABLE build_result
            WORKING_DIRECTORY ${VTK_BUILD_DIR}
        )

        if(build_result EQUAL 0)
            message(STATUS "✅ VTK 编译成功")

            # 安装VTK
            message(STATUS "📦 正在安装 VTK...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build ${VTK_BUILD_DIR} --target install --config ${VTK_ACTUAL_BUILD_TYPE}
                RESULT_VARIABLE install_result
                WORKING_DIRECTORY ${VTK_BUILD_DIR}
            )

            if(install_result EQUAL 0)
                message(STATUS "✅ VTK 安装成功")
                set(VTK_DIR "${VTK_INSTALL_DIR}/lib/cmake/vtk-${VTK_VERSION_MAJOR}" CACHE PATH "VTK directory")
                find_package(VTK ${VTK_VERSION} REQUIRED PATHS ${VTK_DIR})
                set(VTK_FOUND TRUE)
            else()
                message(FATAL_ERROR "❌ VTK 安装失败")
            endif()
        else()
            # 输出详细错误信息
            message(STATUS "❌ VTK 编译失败，返回码: ${build_result}")

            # 检查是否有输出
            if(build_output)
                message(STATUS "编译输出（最后1000字符）:")
                string(LENGTH "${build_output}" output_len)
                if(output_len GREATER 1000)
                    string(SUBSTRING "${build_output}" ${output_len}-1000 -1 build_output_tail)
                    message(STATUS "${build_output_tail}")
                else()
                    message(STATUS "${build_output}")
                endif()
            endif()

            if(build_error)
                message(STATUS "错误信息:")
                message(STATUS "${build_error}")
            endif()

            # 尝试查看最后的错误日志
            file(GLOB error_logs "${VTK_BUILD_DIR}/CMakeFiles/*.log")
            if(error_logs)
                message(STATUS "找到以下日志文件:")
                foreach(log ${error_logs})
                    message(STATUS "  ${log}")
                endforeach()
            endif()

            message(FATAL_ERROR "VTK 编译失败，请查看上面的错误信息")
        endif()
    else()
        message(STATUS "❌ VTK 配置失败，返回码: ${config_result}")
        if(config_output)
            message(STATUS "配置输出:")
            message(STATUS "${config_output}")
        endif()
        if(config_error)
            message(STATUS "错误信息:")
            message(STATUS "${config_error}")
        endif()
        message(FATAL_ERROR "VTK 配置失败，请查看上面的错误信息")
    endif()

elseif(VTK_STATUS STREQUAL "ZIP_ONLY")
    # 解压并构建
    message(STATUS "📦 解压 VTK 源码...")

    # 确保源码目录不存在
    if(EXISTS "${VTK_SOURCE_DIR}")
        message(STATUS "⚠️  清理旧的源码目录...")
        file(REMOVE_RECURSE "${VTK_SOURCE_DIR}")
    endif()

    # 解压到临时目录
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${VTK_ZIP_FILE}
        WORKING_DIRECTORY ${VTK_CACHE_DIR}
        RESULT_VARIABLE extract_result
        OUTPUT_QUIET
        ERROR_VARIABLE extract_error
    )

    if(extract_result EQUAL 0)
        # 检查解压后的目录名（可能是VTK-9.3.1或其他）
        file(GLOB VTK_EXTRACTED_DIRS "${VTK_CACHE_DIR}/VTK-*")
        list(GET VTK_EXTRACTED_DIRS 0 VTK_EXTRACTED_DIR)

        if(EXISTS "${VTK_EXTRACTED_DIR}")
            # 移动到标准源码目录
            file(RENAME "${VTK_EXTRACTED_DIR}" "${VTK_SOURCE_DIR}")
            message(STATUS "✅ VTK 解压成功")

            # 更新状态并重新处理
            set(HAS_SOURCE TRUE)
            set(VTK_STATUS "SOURCE_ONLY")
            message(STATUS "🔄 切换到源码构建模式...")

            # 递归调用处理SOURCE_ONLY状态
            include(${CMAKE_CURRENT_LIST_FILE})
        else()
            message(FATAL_ERROR "❌ 解压后未找到VTK目录")
        endif()
    else()
        message(WARNING "❌ VTK 解压失败: ${extract_error}")
        message(STATUS "🔄 删除损坏的压缩包并重新下载...")
        file(REMOVE "${VTK_ZIP_FILE}")
        set(VTK_STATUS "NONE")
        include(${CMAKE_CURRENT_LIST_FILE})
    endif()

else()
    # 需要下载
    message(STATUS "⬇️  开始下载 VTK ${VTK_VERSION}...")

    # 使用 file(DOWNLOAD) 直接下载，避免创建 subbuild 目录
    set(VTK_DOWNLOAD_URL "https://www.vtk.org/files/release/${VTK_VERSION_MAJOR}/VTK-${VTK_VERSION}.tar.gz")

    message(STATUS "📥 正在下载 VTK (约200MB)...")
    message(STATUS "   URL: ${VTK_DOWNLOAD_URL}")

    gplatform_download(
        ${VTK_DOWNLOAD_URL}
        ${VTK_ZIP_FILE}
        EXPECTED_HASH SHA256=${VTK_EXPECTED_SHA256}
        STATUS download_status
        LOG download_log
        SHOW_PROGRESS
    )

    list(GET download_status 0 status_code)
    list(GET download_status 1 status_msg)

    if(NOT status_code EQUAL 0)
        message(FATAL_ERROR "❌ VTK 下载失败: ${status_msg}\n   ${download_log}")
    endif()

    message(STATUS "✅ VTK 下载成功 (SHA256 校验通过)")

    # 解压
    message(STATUS "📦 解压 VTK 源码...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${VTK_ZIP_FILE}
        WORKING_DIRECTORY ${VTK_CACHE_DIR}
        RESULT_VARIABLE extract_result
    )

    if(extract_result EQUAL 0)
        message(STATUS "✅ VTK 下载和解压成功")
        # 递归调用自己来处理SOURCE_ONLY状态
        set(VTK_STATUS "SOURCE_ONLY")
        include(${CMAKE_CURRENT_LIST_FILE})
    else()
        message(FATAL_ERROR "❌ VTK 解压失败")
    endif()
endif()

# 设置VTK可用标志
if(VTK_FOUND)
    set(VTK_AVAILABLE TRUE CACHE BOOL "VTK library is available")
    message(STATUS "")
    message(STATUS "🎯 VTK 库已就绪")
    message(STATUS "   版本: ${VTK_VERSION}")
    message(STATUS "   安装目录: ${VTK_INSTALL_DIR}")

    # 输出VTK模块信息
    if(VTK_AVAILABLE_COMPONENTS)
        message(STATUS "   可用模块: ${VTK_AVAILABLE_COMPONENTS}")
    endif()
else()
    set(VTK_AVAILABLE FALSE CACHE BOOL "VTK library is not available")
    message(WARNING "⚠️  VTK 库不可用")
endif()

# 便利函数：将VTK库链接到目标
function(link_vtk_to_target target_name)
    if(VTK_AVAILABLE)
        # 确定需要的VTK组件
        set(VTK_COMPONENTS
            CommonCore
            CommonDataModel
            CommonSystem
            FiltersSources
            FiltersGeometry
            FiltersModeling
            InteractionStyle
            InteractionWidgets
            RenderingCore
            RenderingOpenGL2
            RenderingAnnotation
        )

        # 如果Qt可用，添加Qt相关组件
        if(Qt6_FOUND AND TARGET VTK::GUISupportQt)
            list(APPEND VTK_COMPONENTS
                RenderingQt
                GUISupportQt
            )

            # 检查是否有QtQuick支持
            if(TARGET VTK::GUISupportQtQuick)
                list(APPEND VTK_COMPONENTS GUISupportQtQuick)
            endif()

            if(TARGET VTK::ViewsQt)
                list(APPEND VTK_COMPONENTS ViewsQt)
            endif()
        endif()

        # 链接VTK库
        target_link_libraries(${target_name}
            PRIVATE ${VTK_LIBRARIES}
        )

        # 添加VTK包含目录
        target_include_directories(${target_name}
            PRIVATE ${VTK_INCLUDE_DIRS}
        )

        # 如果使用模块化VTK
        if(VTK_VERSION VERSION_GREATER_EQUAL "9.0")
            vtk_module_autoinit(
                TARGETS ${target_name}
                MODULES ${VTK_LIBRARIES}
            )
        endif()

        message(STATUS "✅ 已将 VTK 库链接到目标: ${target_name}")

        # 显示链接的模块信息
        if(Qt6_FOUND AND TARGET VTK::GUISupportQt)
            message(STATUS "   包含Qt支持模块")
        endif()
    else()
        message(WARNING "⚠️  目标 ${target_name} 无法链接 VTK")
    endif()
endfunction()

message(STATUS "")
message(STATUS "📖 VTK缓存系统使用说明:")
message(STATUS "   1. 设置环境变量: export CMAKE_FETCH_CACHE=$HOME/.cmake_fetch_cache")
message(STATUS "   2. 首次编译会下载并缓存 VTK 库 (需要较长时间)")
message(STATUS "   3. 后续编译将直接使用缓存，大幅提升速度")
message(STATUS "   4. 不同的CMAKE_BUILD_TYPE会使用不同的构建目录")
message(STATUS "   5. 安装Ninja可显著加速编译: brew install ninja (macOS) 或 apt install ninja-build (Linux)")
message(STATUS "   6. 清理方法: 删除 ${VTK_CACHE_DIR} 目录")
message(STATUS "")
