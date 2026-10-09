# GeoPlatform-App

GPlatform 应用开发仓库，使用 CMake 编译上层源码并链接预编译的 [GeoPlatform-SDK](https://github.com/dezbracaty/GeoPlatform-SDK)。
开发应用只需本仓库及 SDK 子模块，无需完整源码主仓库。

## 可开发的源码

| 目录 | 职责 |
| --- | --- |
| `src/Base/` | BaseDB、BaseTypes、BaseGeometry、BaseUI、BaseInteraction、BaseRender |
| `src/AppDB/` | 应用 DB 类型，可按业务需求新增和扩展 |
| `src/Document/` | 文档与对象管理 |
| `src/` 中其他模块 | 应用组合、交互、切片、打印机、服务等业务功能 |
| `qml/`、`resources/` | 页面、组件及应用资源 |
| `ThirdParty/libs/GPlatformSDK/` | 预编译依赖的 Git 子模块 |

底层事务实现、渲染实现和内置 VTK/Filament 后端由 SDK 提供。
BaseRender 源码提供渲染器工厂、会话等扩展接口；内置后端仍依赖部分应用 DB 类型。
新增 DB 不会自动得到内置后端的绘制支持。修改 SDK 共享类的布局、虚函数表或接口签名，需要使用重新编译的配套 SDK。

## 获取代码与 SDK

先安装 Git LFS，然后执行：

```bash
git lfs install
git clone --recurse-submodules https://github.com/dezbracaty/GeoPlatform-App.git
cd GeoPlatform-App
git -C ThirdParty/libs/GPlatformSDK lfs pull
```

SDK 路径为 `ThirdParty/libs/GPlatformSDK/<架构>/<构建类型>/`，版本由子模块提交指针决定。
查看所选版本中的 `sdk-build.json`，确认平台、架构、Qt 版本及工具链匹配。
本版本提供 macOS `arm64/Release` 和 Windows `AMD64/Release`。
Windows SDK 使用 MSVC 19.44、Qt 6.11.2；macOS SDK 使用 AppleClang 21、Qt 6.9.3。
拉取已有子模块不会生成尚未发布的平台产物。

## CMake 编译

安装 CMake 3.24+、Ninja、Python 3，以及与 SDK 匹配的 Qt 开发环境。
设置 `GPLATFORM_QT_ROOT` 环境变量，指向包含 `lib/cmake/Qt6/Qt6Config.cmake` 的 Qt SDK。
Windows 使用 MSVC x64 和 MSVC Qt；需要 Visual Studio C++ 工作负载与 Windows SDK。

Windows 普通终端：

```powershell
$env:GPLATFORM_QT_ROOT = "C:/path/to/Qt/msvc2022_64"
scripts/build.cmd -Check
scripts/build.cmd
```

macOS/Linux：

```bash
export GPLATFORM_QT_ROOT=/path/to/matching/Qt
bash scripts/build.sh --check
bash scripts/build.sh
```

已经准备好编译器环境的终端也可以直接执行 `cmake --preset release` 和 `cmake --build --preset release`。
VS Code 使用 CMake Tools 的 `release`/`debug` 预设；Windows 开发环境由插件准备。
修改环境变量后需要重启 IDE。更换编译器或 Qt 时使用新的构建目录。

主项目和独立 App 共用本子树中的构建启动器、Qt 输入和 MSVC/Ninja 兼容处理。
CMake 根据实际编译器识别架构；不要求存在 `VSCMD_ARG_TGT_ARCH`。
`check` 检查编译器与 Qt，不加载 SDK；完整配置才会验证并加载指定版本的 SDK。
独立构建使用 App 自身的构建目录和 SDK 子模块，不读取主项目依赖缓存、源码或测试入口。

CMake 会编译本仓库的 Base、AppDB、Document 和业务源码，并链接 SDK 中的库。
SDK 动态库会自动复制到应用包中。Qt 开发环境由本机提供；对外分发应用还需完成 Qt 部署与应用签名。
`debug` 预设需要 SDK 中已有相应架构的 `Debug/` 产物，不能混用 Release 库。

## 更新代码

```bash
git pull --ff-only
git submodule update --init --recursive
git -C ThirdParty/libs/GPlatformSDK lfs pull
cmake --preset release
cmake --build --preset release
```

使用仓库记录的子模块指针，不使用 `git submodule update --remote` 自动追踪 SDK 最新分支。
错误会区分 SDK 子模块未初始化、该版本没有当前平台/构建类型，以及 LFS 文件未下载。
按对应提示处理；平台包缺失需要发布配套 SDK 并更新子模块指针。

## 可选扩展检查

```bash
cmake --preset release -DGPLATFORM_BUILD_SDK_CHECKS=ON
cmake --build build-Release --target GPlatform SDKConsumerExtensionCheck
ctest --test-dir build-Release -R '^SDKConsumerExtensionCheck$' --output-on-failure
```

该检查验证应用侧自定义 DB 的注册、文档使用及渲染器工厂注册，不覆盖自定义 DB 的实际绘制。
