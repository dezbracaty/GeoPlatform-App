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
SDK 格式 2 提供内部库交付；配套版本由当前 App 记录的 SDK gitlink 决定。
旧版本记录的 SDK SHA 需要从迁移归档恢复；不将旧完整 SDK 混入当前交付。
Git App 根据真实 Git 受管文件及 HEAD 校验 SDK 绑定的内容摘要，无需导出清单；没有 Git 元数据的文件包必须提供 `ThirdParty/source-inputs.json` 并通过同一摘要校验。

## CMake 编译

请使用与 SDK 二进制兼容的 C++ 工具链及 Qt；实际生产环境记录在所选 `<架构>/<构建类型>/sdk-build.json`。当前独立 App 要求 Qt 版本完全匹配；Windows 的 MSVC 主次版本及运行库也必须与 SDK 匹配。
Windows 使用 MSVC 开发环境，macOS 最低版本为 15。
安装 CMake 3.24+、Ninja、配套 Qt（含 Quick、QuickControls2、Quick3D、Network、Concurrent、Multimedia 等模块），
将 Ninja 等构建工具加入 PATH；macOS/Linux 将所选 CMake 加入 PATH。下面是 Windows/Linux 的终端示例；
macOS Apple Silicon 使用 `arm_release`，Intel 使用 `x86_release`。VS Code 用户使用下述界面入口即可。
在本仓库根目录执行：

```bash
cmake --preset release
cmake --build --preset release
```

VS Code 用户用文件资源管理器将 [vscode-template](vscode-template) 整个目录复制为本仓库的 `.vscode`，安装推荐的 CMake Tools、C++ 扩展，在 `settings.json` 中填写：

- `cmake.cmakePath`：本机 CMake 可执行文件路径；已在 VS Code 的 PATH 中时可保留 `cmake`。Windows 使用 VS 自带 `cmake.exe`。
- `cmake.configureEnvironment.Qt6_DIR`：包含本机所选 `Qt6Config.cmake` 的目录。

模板显式显示底部 CMake 状态栏。在状态栏或 CMake 项目面板选择配置预设和同名构建预设，点击 Configure，再点击 Build；也可使用默认构建快捷键。
macOS 只显示六个预设：`arm_release`、`arm_debug`、`x86_release`、`x86_debug`、`universal_release`、`universal_debug`。
目录分别为 `build-arm64-Release/Debug`、`build-x86_64-Release/Debug`、`build-universal-Release/Debug`（Release、Debug 为两个不同目录）。
`x86` 指 64 位 Intel；Windows/Linux 只显示 `release/debug`，目录为 `build-Release/build-Debug`。
架构由预设选择，无需修改 settings 或填写命令行参数。完整源码主仓库与独立 App 使用相同的预设名和模板，
前者构建应用及 SDK 导出产物，后者只构建 App。
整个 `.vscode/` 和 `CMakeUserPresets.json` 均为本机文件，默认忽略。已有个人设置时按模板补充，不覆盖原有内容。

Qt 路径占位符必须替换。若 Ninja 或 Python 不在 VS Code 的 PATH 中，可在
`cmake.configureEnvironment` 中补充 `CMAKE_MAKE_PROGRAM`（Ninja 可执行文件完整路径）和
`Python3_EXECUTABLE`（Python 3 可执行文件完整路径），不需要在终端导出环境。
Windows 的 MSVC 开发环境由 CMake Tools 加载；macOS 使用系统选中的 Xcode。
独立 App 必须有对应架构及配置的 SDK 包，尤其 Debug 不能使用 Release SDK；缺少时 CMake 会列出可用包。
Apple Silicon 上构建/运行 Intel 程序需要 Rosetta。Universal 预设自动构建同配置的两个架构，部署 Qt、合并并验证所有运行时二进制及签名；
输出为 `build-universal-<配置>/GPlatform.app`。完整源码主仓库与独立 App 使用同一套 Universal 实现，
独立 App 的两侧 SDK 包均须存在，Debug 不能回退到 Release。

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
App 检查 SDK 的平台、格式、编译器及 MSVC 工具链和运行库，根据 SDK 元数据校验依赖版本与开关，查找匹配的 Qt，再用与根项目相同的 fetch 规则准备第三方目标，最后导入内部 SDK。
VTK、OCCT、RTTR、Embree、spdlog 等从原有机器级缓存使用配套安装的库和头文件；兼容缓存可复用，缺失时由原有规则下载或构建。Eigen/cereal 是头文件库，Filament 沿用官方预编译包。
SmoothUI、SmoothUIDock/KDDockWidgets 和 miniz 在 App 构建中从子模块源码编译；libslicer 使用原有独立构建缓存。其缓存键含源码绝对路径，因此不同 checkout 首次可能重新编译 libslicer，不迁移缓存或复用含其他源码路径的构建树。
不混用旧完整 SDK 格式或不同平台/配置的头文件；没有格式 2 的平台槽会明确报缺失，需要根项目在相应平台生产。
不提供单独的环境检查入口；preset 和共享 fetch 入口不额外限制并发数。OrcaSlicer 自身依赖脚本保留其已有构建行为。
Windows 必须提供有效的 `Qt6_DIR`；缺失或目录不含 `Qt6Config.cmake` 时，在 `project()` 前报错停止。
可通过环境变量、`-DQt6_DIR=...` 或本地 CMake 配置提供，已有合法缓存也算已提供；显式 CMake 值优先于环境默认值。
Windows 只查找所选 Qt 目录，不自动改用其他安装。macOS/Linux 保留标准 Qt6_DIR、prefix 和 Kit 查找方式。
Qt 组件查找失败时会停止配置，保留原始诊断，并提示标准路径配置示例；其他 SDK 依赖错误不会被当作 Qt 配置错误。
不在仓库中固定安装路径。Qt 版本须与 SDK 匹配；更换 Qt 或依赖 ABI 时重建配套 SDK，并使用新的项目构建目录。
仅修改环境不会覆盖旧缓存，无需清理机器级依赖缓存。纯 build 未触发重新 configure 时不会执行这些输入检查。
主项目打包生成的 App 文件目录带有实物 SDK 和已初始化的第三方受管源码，直接使用以上命令；无需伪造 Git 仓库或初始化。`ThirdParty/source-inputs.json` 绑定实际源码文件摘要；匹配的未提交修改也可构建，内容变化后须重新导出配套 SDK。
正式 Git 仓库使用者则需按上面的步骤初始化 submodule/LFS。两种交付状态应分别验证。
SDK 动态库会自动复制到应用包中。Qt 开发环境由本机提供；Windows 安装包通过下面的目标完成 Qt 部署，应用签名由发布者处理。
libslicer 运行库和预设资源由现有部署任务跟踪；增量更新运行库时，即使主程序不重新链接，也会复制到运行目录。
`debug` 预设需要 SDK 中已有相应架构的 `Debug/` 产物，不能混用 Release 库。

## Windows 安装包

安装 Inno Setup 7，并完成上面的 Release configure 后，在 App 根目录执行：

```powershell
& $CMakePath --build build-Release --target GPlatform_installer
```

该目标依次构建程序、重新生成 `dist/`（部署 Qt、依赖 DLL 和切片资源），再编译安装模板。
输出为 App 自己的 `package/CFSlicer-<版本号>-<YYYYMMDD>.exe`，例如 `CFSlicer-0.0.3-20261016.exe`；日期取打包当天。内置 VC++ x64 运行时，安装时按需安装运行时。
CMake 从 PATH 和 Inno Setup 7 的标准用户/系统安装位置查找 ISCC。
非标准位置可在 configure 时指定 `-DISCC_EXECUTABLE="<安装目录>/ISCC.exe"`。
缺少 ISCC 只会让安装包目标报错，普通 configure 和编译仍可使用；安装工具后重新 configure。
此目标仅用于 Windows Release，不自动安装工具、运行安装器或发布产物。

安装向导提供两种互斥的安装方式：

- **全新安装（重置全部设置）**：更新程序，并用包内 `GPlatform.defaults.ini` 整份替换启动安装器用户的 `%LOCALAPPDATA%\GPlatform\GPlatform\GPlatform.ini`。旧键和值全部清除，默认中文、最小化导航窗格；模型和项目文件保留。
- **覆盖安装（保留全部设置）**：只更新程序，用户 INI 原文件保持不变。

检测到已有用户 INI 时默认选择覆盖安装，否则默认选择全新安装；不会记住上次的重置选择。需要静默安装时，可用 Inno Setup 标准参数 `/TASKS="settingskeep"` 或 `/TASKS="settingsreset"` 明确选择，未指定则使用相同的自动选择规则。
默认 INI 由仓库 `config/GPlatform.defaults.ini` 维护，路径项留空后由程序采用当前用户的默认目录，不复制打包机器的个人配置。
配置检测和替换在安装器原始启动用户身份下执行，UAC 使用另一个管理员账户授权时也不会改写该管理员的配置。请正常双击安装器，再按 UAC 提示授权；预先以其他账户启动安装器时，该账户本身就是安装器的启动用户。
安装期间通过现有应用关闭流程退出旧程序，完成后由最后一页的启动选项决定是否打开新程序。配置操作仅发生在安装阶段，日常启动不执行版本重置。

## 更新代码

以下终端示例为 Windows/Linux；macOS 将 `release` 替换为对应架构的预设。
在 VS Code 中也可通过 Git 界面更新，再对当前预设执行 Configure/Build。

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

下面以 Windows/Linux 为例；macOS 的预设和构建目录按上面的矩阵替换。

```bash
cmake --preset release -DGPLATFORM_BUILD_SDK_CHECKS=ON
cmake --build build-Release --target SDKConsumerExtensionCheck
ctest --test-dir build-Release -R '^SDKConsumerExtensionCheck$' --output-on-failure
```

该检查验证应用侧自定义 DB 的注册、文档使用及渲染器工厂注册，不覆盖自定义 DB 的实际绘制。

## 脚本与验收工具

`cmake/` 负责接入 CMake；`scripts/` 包含独立 App 随包交付的构建、部署和诊断实现。
完整仓库的根目录兼容脚本转发到这些实现，SDK 的生产和发布编排留在完整仓库。
终端辅助入口为 `scripts/build.sh`（macOS/Linux）与 `scripts/build.cmd`（Windows），均使用本仓库 presets。
macOS 双架构实现为 `scripts/build_macos_universal.py`，默认目录与 preset 一致；
普通 `GPlatform_dist` 调用 `scripts/deploy_macos.py`，两者共用 `scripts/macos_bundle.py` 的部署清理和依赖审计。
Release 薄架构配置完成后，可在所选构建目录构建 `GPlatform_dist`，输出位于本仓库 `dist/`。
该目标先构建应用，再部署、临时签名、检查依赖并生成 DMG；正式发布还需要签名和公证。

截图必须通过 `scripts/auto_screenshot.sh --app /path/to/GPlatform.app` 指定本次构建的产物。
崩溃分析使用 `scripts/analyze_crash.sh`，通过 PATH、`MINIDUMP_STACKWALK` 或 `--stackwalk` 选择工具，
可用 `--symbols` 指定符号目录；工具失败会原样返回失败码。

`tests/startup/check_translations.py` 属于 App 自身检查；
`tests/startup/run_window_checks.py` 在 Windows 上运行，需要显式提供 `--application`、`--journal-tools` 和 `--output`。
依赖 `Core/tests/journals` 的批量业务场景验收器位于完整仓库 `scripts/run_journals.py`，不随独立 App 交付。

Zed 用户可将 `zed-template/` 复制为本地 `.zed/`。当前模板提供 macOS/Linux Configure、Build，
共用 `scripts/build.sh`；从编辑器环境设置 `Qt6_DIR`，可用 `GPLATFORM_PRESET` 选择预设，默认本机架构 Release。
`.zed/` 与 `.vscode/` 一样默认忽略；复制模板前保留原有个人设置。

## GitHub Actions

App 仓库的 Windows / macOS 自动构建、手动选项与分支菜单规则见 [CI 使用说明](docs/ci.md)。
