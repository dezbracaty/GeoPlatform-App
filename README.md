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
git clone --branch fix/sdk-packaging-restart --recurse-submodules https://github.com/dezbracaty/GeoPlatform-App.git
cd GeoPlatform-App
git -C ThirdParty/libs/GPlatformSDK lfs pull
```

SDK 路径固定为 `ThirdParty/libs/GPlatformSDK/<架构>/<构建类型>/`。
版本由本仓库记录的子模块提交指针决定，无需在目录名中写版本。

## CMake 编译

请使用与 SDK 配套的 C++ 工具链及相同 Qt 版本；实际构建信息见所选 `<架构>/<构建类型>/sdk-build.json`。
Windows 使用 MSVC 开发环境，macOS 最低版本为 15。
安装 CMake 3.24+、Ninja、配套 Qt（含 Quick、QuickControls2、Quick3D、Network、Concurrent、Multimedia 等模块），
将 CMake 和 Ninja 加入 PATH。在本仓库根目录执行：

```bash
cmake --preset release
cmake --build --preset release
```

Windows 本次验收使用 Visual Studio 自带的 CMake。在已配置 MSVC 开发环境和 Qt 搜索路径的 PowerShell 中，显式选择本机对应安装路径：

```powershell
$CMakePath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $CMakePath --preset release
& $CMakePath --build --preset release
```

`CMakePath` 是终端变量，用来选择启动工具；不是项目要求新增的 CMake 配置变量。

CMake 会编译本仓库的 Base、AppDB、Document 和业务源码，并链接 SDK 中的库。
SDK 中的依赖库与其对应构建的公开头文件、生成的配置头文件一起提供；App 不再次 fetch 或编译这些库。
不同平台的生成头文件可能不同，不混用其他平台或其他构建配置的头文件。
不提供单独的环境检查入口；构建 preset 不固定并发数。
Qt 由本机提供：`Qt6_DIR` 指向包含 `Qt6Config.cmake` 的目录，或用 `CMAKE_PREFIX_PATH` 指向 Qt 安装根目录；也可由 IDE 的 Qt Kit 传入。无需同时设置两个变量。
SDK 的 Qt 依赖查找失败时会停止配置，保留原始诊断，并提示 SDK 所需版本和标准路径配置示例；其他 SDK 依赖错误不会被当作 Qt 配置错误。
变量可在用户环境或本地 CMake 配置中设置，不在仓库中固定安装路径。Qt 版本须与 SDK 匹配；更换 Qt 时使用新的构建目录。
主项目打包生成的 App 文件目录已经带有实物 SDK，直接使用以上命令；无需伪造 Git 仓库或初始化。
正式 Git 仓库使用者则需按上面的步骤初始化 submodule/LFS。两种交付状态应分别验证。
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
如果提示 SDK 缺失，请检查子模块和构建类型；如果提示 Git LFS 指针，请在 SDK 子模块中执行 `git lfs pull`。

## 可选扩展检查

```bash
cmake --preset release -DGPLATFORM_BUILD_SDK_CHECKS=ON
cmake --build build-Release --target SDKConsumerExtensionCheck
ctest --test-dir build-Release -R '^SDKConsumerExtensionCheck$' --output-on-failure
```

该检查验证应用侧自定义 DB 的注册、文档使用及渲染器工厂注册，不覆盖自定义 DB 的实际绘制。
