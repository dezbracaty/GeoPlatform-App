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
| `ThirdParty/SmoothUI/`、`SmoothUIDock/`、`OrcaSlicer/` | 根项目和 App 共用的第三方源码子模块 |
| `cmake/GPlatformThirdParty.cmake`、`cmake/thirdparty/` | 共用的第三方依赖入口与原有 fetch 规则 |
| `ThirdParty/libs/GPlatformSDK/` | 项目内部库及其公开接口的二进制子模块 |

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

SDK 路径固定为 `ThirdParty/libs/GPlatformSDK/<架构>/<构建类型>/`。
版本由本仓库记录的子模块提交指针决定，无需在目录名中写版本。
当前支持入口是默认分支 `fix/sdk-packaging-restart`；SDK 同名重建后仅提供格式 2 的新交付历史。
旧版本记录的 SDK SHA 需要从迁移归档恢复；不将旧完整 SDK 混入当前交付。
Git App 同时检查第三方子模块提交及受管文件修改；改变与 SDK 共用的第三方输入，需要根项目重新生产匹配 SDK。

## CMake 编译

请使用与 SDK 配套的 C++ 工具链及相同 Qt 版本；实际构建信息见所选 `<架构>/<构建类型>/sdk-build.json`。
Windows 使用 MSVC 开发环境，macOS 最低版本为 15。
安装 CMake 3.24+、Ninja、配套 Qt（含 Quick、QuickControls2、Quick3D、Network、Concurrent、Multimedia 等模块），
将 Ninja 等构建工具加入 PATH；macOS/Linux 将所选 CMake 加入 PATH。在本仓库根目录执行：

```bash
cmake --preset release
cmake --build --preset release
```

Windows 使用 Visual Studio 自带的 CMake。VS Code 用户将 [settings.json.example](settings.json.example) 复制到本仓库的 `.vscode/settings.json`，填写：

- `cmake.cmakePath`：VS 自带 `cmake.exe` 的完整路径。
- `cmake.configureEnvironment.Qt6_DIR`：包含 `Qt6Config.cmake` 的目录，须匹配 SDK 的 Qt 版本。

安装 CMake Tools 扩展，选择 `release` configure/build preset，依次执行 CMake: Configure 和 CMake: Build。
模板中的占位路径必须替换；无效的 CMake 路径由 CMake Tools 报错。未启用模板时仍可能从 PATH 自动选择 CMake。
个人 `.vscode/settings.json` 不提交；主项目和独立 App 共用这份模板。

命令行用户在已配置 MSVC 开发环境的 PowerShell 中显式选择本机安装路径和 Qt：

```powershell
$CMakePath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$env:Qt6_DIR = 'C:/path/to/Qt/msvc2022_64/lib/cmake/Qt6'
& $CMakePath --preset release
& $CMakePath --build --preset release
```

`CMakePath` 是终端变量，用来选择启动工具；不是项目要求新增的 CMake 配置变量。VS Code 的 `cmake.cmakePath` 不影响终端中的裸 `cmake` 命令。
上面的通用命令和下面的更新、扩展检查命令在 Windows 下也应使用所选 VS CMake 执行。

CMake 会编译本仓库的 Base、AppDB、Document 和业务源码，并链接 SDK 中的库。
SDK 格式 2 只提供内部库（含 Tr、TransDB、JournalSystem）及对应构建的公开/生成头文件，不包含第三方库。
App 先读取 SDK 的依赖版本、特性和源码子模块提交要求，查找本机匹配 Qt，再用与根项目相同的 fetch 规则准备第三方目标，最后导入内部 SDK。
VTK、OCCT、RTTR、Embree、spdlog 等从原有机器级缓存使用配套安装的库和头文件；兼容缓存可复用，缺失时由原有规则下载或构建。Eigen/cereal 是头文件库，Filament 沿用官方预编译包。
SmoothUI、SmoothUIDock/KDDockWidgets 和 miniz 在 App 构建中从子模块源码编译；libslicer 使用原有独立构建缓存。其缓存键含源码绝对路径，因此不同 checkout 首次可能重新编译 libslicer，不迁移缓存或复用含其他源码路径的构建树。
不混用旧完整 SDK 格式或不同平台/配置的头文件；没有格式 2 的平台槽会明确报缺失，需要根项目在相应平台生产。
不提供单独的环境检查入口；preset 和共享 fetch 入口不额外限制并发数。OrcaSlicer 自身依赖脚本保留其已有构建行为。
Windows 必须提供有效的 `Qt6_DIR`；缺失或目录不含 `Qt6Config.cmake` 时，在 `project()` 前报错停止。
可通过环境变量、`-DQt6_DIR=...` 或本地 CMake 配置提供，已有合法缓存也算已提供；显式 CMake 值优先于环境默认值。
Windows 只查找所选 Qt 目录，不自动改用其他安装。macOS/Linux 保留标准 Qt6_DIR、prefix 和 Kit 查找方式。
SDK 的 Qt 依赖查找失败时会停止配置，保留原始诊断，并提示 SDK 所需版本和标准路径配置示例；其他 SDK 依赖错误不会被当作 Qt 配置错误。
不在仓库中固定安装路径。Qt 版本须与 SDK 匹配；更换 Qt 时清理该项目配置缓存或使用新的构建目录。
仅修改环境不会覆盖旧缓存，无需清理机器级依赖缓存。纯 build 未触发重新 configure 时不会执行这些输入检查。
主项目打包生成的 App 文件目录带有实物 SDK 和已初始化的第三方受管源码，直接使用以上命令；无需伪造 Git 仓库或初始化。`ThirdParty/source-revisions.json` 记录生产输入来源，不能证明文件未被后续修改；包的实物校验和真实构建仍需验证。
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
