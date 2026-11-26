# GeoPlatform-App

GPlatform 应用开发仓库，使用 CMake 编译上层源码并链接预编译的 [GeoPlatform-SDK](https://github.com/dezbracaty/GeoPlatform-SDK)。
开发应用只需本仓库、所选平台 SDK 和 Orca 子模块，无需完整源码主仓库。

## 可开发的源码

| 目录 | 职责 |
| --- | --- |
| `src/Base/` | BaseDB、BaseTypes、BaseGeometry、BaseUI、BaseInteraction、BaseRender |
| `src/AppDB/` | 应用 DB 类型，可按业务需求新增和扩展 |
| `src/Document/` | 文档与对象管理 |
| `src/` 中其他模块 | 应用组合、交互、切片、打印机、服务等业务功能 |
| `qml/`、`resources/` | 页面、组件及应用资源 |
| `ThirdParty/OrcaSlicer/` | 保留源码交付的切片依赖 |
| `cmake/GPlatformThirdParty.cmake`、`cmake/thirdparty/` | 共用的第三方依赖入口与原有 fetch 规则 |
| `cmake/sdk-repository.json` | SDK 仓库地址声明；实际版本由平台 SDK 完整 SHA 选择 |

底层事务实现、通用渲染基类、线程、Session、帧交换和原生资源管理由 SDK 提供。
`src/Rendering/` 提供 VTK/Filament 的具体 DBSync、业务注册和模型材质规则，随 App 源码编译。
新增业务 DB 的绘制支持或修改具体 DBSync，只需修改并编译 App；SDK 通过公开的基类及工厂声明支持继承和注册。
修改 SDK 基类布局、虚函数表或运行机制时，仍需重新生产配套 SDK。BaseRender 与界面继续使用后端无关的 Session/帧/拾取契约。

## 获取代码与 SDK

先安装 Git LFS，然后执行：

```bash
git lfs install
git clone --recurse-submodules https://github.com/dezbracaty/GeoPlatform-App.git
cd GeoPlatform-App
git clone --branch windows https://github.com/dezbracaty/GeoPlatform-SDK.git ../GeoPlatform-SDK
git -C ../GeoPlatform-SDK lfs pull
```

上例 Windows 使用默认 SDK 分支 `windows`，Mac 默认使用 `macos`。平台 CI 还支持显式 ref 和 App 同名 SDK 分支。正式候选应先检出所选平台的完整 SDK SHA，再执行 LFS pull；不要在同一轮构建中重新解析前进中的分支。
SDK checkout 内按 `<架构>/<构建类型>/` 分槽，文件包则按 `SDK/<架构>/<构建类型>/` 分槽。App 不再维护 SDK gitlink，也不自动回退旧 SDK。所有本地与 CI 配置均必须显式提供 `GPLATFORM_SDK_ROOT`；该路径指向 checkout 根或导出包的 SDK 目录。
SDK 格式 3 提供内部库与 UI 二进制/同源 QML 资源；SmoothUI、SmoothUIDock 及其 KD 依赖不需要私有源码 checkout。平台 CI 和参数见 [docs/ci.md](docs/ci.md)。缺少新 SDK 地址声明或不满足现有构建/SDK/Server 契约的历史 App 不使用旧入口兼容。
Git App 根据真实 Git 受管文件及 HEAD 校验 SDK 绑定的内容摘要，无需导出清单；没有 Git 元数据的文件包必须提供 `ThirdParty/source-inputs.json` 并通过同一摘要校验。

## CMake 编译

请使用与 SDK 二进制兼容的 C++ 工具链及 Qt；实际生产环境记录在所选 `<架构>/<构建类型>/sdk-build.json`。当前独立 App 要求 Qt 版本完全匹配；Windows 的 MSVC 主次版本及运行库也必须与 SDK 匹配。
Windows 使用 MSVC 开发环境，macOS 最低版本为 15。
安装 CMake 3.24+、Ninja、配套 Qt（含 Quick、QuickControls2、Quick3D、Network、Concurrent、Multimedia 等模块），
将 Ninja 等构建工具加入 PATH；macOS/Linux 将所选 CMake 加入 PATH。下面是 Windows/Linux 的终端示例；
macOS Apple Silicon 使用 `arm_release`，Intel 使用 `x86_release`。VS Code 用户使用下述界面入口即可。
在本仓库根目录执行：

```bash
cmake --preset release -DGPLATFORM_SDK_ROOT=../GeoPlatform-SDK
cmake --build --preset release
```

VS Code 用户用文件资源管理器将 [vscode-template](vscode-template) 整个目录复制为本仓库的 `.vscode`，安装推荐的 CMake Tools、C++ 扩展，在 `settings.json` 中填写：

- `cmake.cmakePath`：本机 CMake 可执行文件路径；已在 VS Code 的 PATH 中时可保留 `cmake`。Windows 使用 VS 自带 `cmake.exe`。
- `cmake.configureSettings.GPLATFORM_SDK_ROOT`：所选平台 SDK checkout 或导出包 SDK 目录的绝对路径。
- `cmake.configureEnvironment.Qt6_DIR`：包含本机所选 `Qt6Config.cmake` 的目录。

模板显式显示底部 CMake 状态栏。在状态栏或 CMake 项目面板选择配置预设和同名构建预设，点击 Configure，再点击 Build；也可使用默认构建快捷键。
macOS 只显示六个预设：`arm_release`、`arm_debug`、`x86_release`、`x86_debug`、`universal_release`、`universal_debug`。
目录分别为 `build-arm64-Release/Debug`、`build-x86_64-Release/Debug`、`build-universal-Release/Debug`（Release、Debug 为两个不同目录）。
`x86` 指 64 位 Intel；Windows/Linux 只显示 `release/debug`，目录为 `build-Release/build-Debug`。
架构由预设选择；SDK_ROOT 与 Qt 路径由本机设置明确提供。完整源码主仓库与独立 App 使用相同的预设名和模板，
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
& $CMakePath --preset release -DGPLATFORM_SDK_ROOT=../GeoPlatform-SDK
& $CMakePath --build --preset release
```

`CMakePath` 是终端变量，用来选择启动工具；不是项目要求新增的 CMake 配置变量。VS Code 的 `cmake.cmakePath` 不影响终端中的裸 `cmake` 命令。
上面的通用命令和下面的更新、扩展检查命令在 Windows 下也应使用所选 VS CMake 执行。

CMake 会编译本仓库的 Base、AppDB、Document 和业务源码，并链接 SDK 中的库。
SDK 格式 3 提供内部库（含 Tr、TransDB、JournalSystem）、SmoothUI/SmoothUIDock/KDDockWidgets 二进制闭包及配套公开声明、同源 QML 资源；其他公开第三方仍由原 fetch provider 提供。
App 检查 SDK 的平台、格式、编译器及 MSVC 工具链和运行库，根据 SDK 元数据校验依赖版本与开关，查找匹配的 Qt，再用与根项目相同的 fetch 规则准备第三方目标，最后导入内部 SDK。
VTK、OCCT、RTTR、Embree、spdlog 等从原有机器级缓存使用配套安装的库和头文件；兼容缓存可复用，缺失时由原有规则下载或构建。Eigen/cereal 是头文件库，Filament 沿用官方预编译包。
SmoothUI、SmoothUIDock/KDDockWidgets 由 SDK 提供；miniz 仍从 Orca 子模块源码编译，libslicer 使用原有独立构建缓存。其缓存键含源码绝对路径，因此不同 checkout 首次可能重新编译 libslicer，不迁移缓存或复用含其他源码路径的构建树。
不混用旧完整 SDK 格式或不同平台/配置的头文件；没有配套格式 3 的平台槽会明确报缺失，需要根项目在相应平台生产。
不提供单独的环境检查入口；preset 和共享 fetch 入口不额外限制并发数。OrcaSlicer 自身依赖脚本保留其已有构建行为。
Windows 必须提供有效的 `Qt6_DIR`；缺失或目录不含 `Qt6Config.cmake` 时，在 `project()` 前报错停止。
可通过环境变量、`-DQt6_DIR=...` 或本地 CMake 配置提供，已有合法缓存也算已提供；显式 CMake 值优先于环境默认值。
Windows 只查找所选 Qt 目录，不自动改用其他安装。macOS/Linux 保留标准 Qt6_DIR、prefix 和 Kit 查找方式。
Qt 组件查找失败时会停止配置，保留原始诊断，并提示标准路径配置示例；其他 SDK 依赖错误不会被当作 Qt 配置错误。
不在仓库中固定安装路径。Qt 版本须与 SDK 匹配；更换 Qt 或依赖 ABI 时重建配套 SDK，并使用新的项目构建目录。
仅修改环境不会覆盖旧缓存，无需清理机器级依赖缓存。纯 build 未触发重新 configure 时不会执行这些输入检查。
SDK 导出器只生成 SDK 文件包，不复制 App 源码或内嵌 SDK 到 App。独立 App 源码与 Orca 输入另行准备，配置时始终显式指定外置 SDK_ROOT。Git checkout 根据实际受管源码校验输入；非 Git 源码文件包须携带完整 ThirdParty/source-inputs.json 并通过相同内容摘要检查，不能伪造 Git 元数据或来源。
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

- **全新安装（重置全部设置）**：更新程序，用包内 `GPlatform.defaults.ini` 替换启动安装器用户的 `%LOCALAPPDATA%\GPlatform\GPlatform.ini`，并清除切片选择记忆。默认中文、最小化导航窗格，用户另存的预设、模型和项目文件保留。
- **覆盖安装（保留全部设置）**：只更新程序，用户 INI 原文件保持不变。

检测到用户 INI 或切片选择记忆时默认选择覆盖安装，否则默认选择全新安装；不会记住上次的重置选择。需要静默安装时，可用 Inno Setup 标准参数 `/TASKS="settingskeep"` 或 `/TASKS="settingsreset"` 明确选择，未指定则使用相同的自动选择规则。
默认 INI 由仓库 `config/GPlatform.defaults.ini` 维护，路径项留空后由程序采用当前用户的默认目录，不复制打包机器的个人配置。
配置检测和替换在安装器原始启动用户身份下执行，UAC 使用另一个管理员账户授权时也不会改写该管理员的配置。请正常双击安装器，再按 UAC 提示授权；预先以其他账户启动安装器时，该账户本身就是安装器的启动用户。
安装期间通过现有应用关闭流程退出旧程序，完成后由最后一页的启动选项决定是否打开新程序。配置操作仅发生在安装阶段，日常启动不执行版本重置。

## 系统文件关联与网页打开

应用支持从系统打开 STL、3MF 和 G-code：STL 沿现有单位确认导入，3MF 保留项目/模型选择及替换确认，G-code 打开预览。
Windows 安装器、macOS App 的文档声明及 Linux 包的 desktop/MIME 配置提供打开方式；不覆盖用户已有的文件默认应用。
需要默认双击打开时，在系统的“打开方式/默认应用”中选择 GPlatform。macOS 使用完整部署的 App，先正常打开一次使系统识别其处理能力。

命令行也可传入一个或多个文件，含空格的路径需要引号。普通启动会将请求转交同用户、同一可执行文件路径的已有实例；不同安装/构建路径彼此隔离。
文件按序处理，最多接纳 32 个未完成项。某项失败或用户取消后继续其余项；启动失败、退出或队列已满会明确拒绝请求。
转交回执仅表示已接纳到内存队列，不代表导入成功或崩溃后的持久化保证。文件/链接参数不会在设置页重启或 Journal 重启时再次导入。
带显式 Journal 专用参数（例如 `--ghost-port`、`--journal-replay-id`）的进程保持独立，不接受外部文件和协议链接。

网页可以通过自有协议唤起浏览器所在电脑的应用：

```javascript
const link = 'gplatform://open?file=' + encodeURIComponent(httpsDownloadUrl);
```

仅接受 HTTPS 模型地址；下载地址没有文件扩展名时，追加 `&name=` 与经过 `encodeURIComponent` 编码的纯文件名，例如 `model.stl`。
文件名限 STL、3MF、G-code，不能包含目录；地址明确为其他格式或与文件名格式冲突时拒绝。
应用显示当前外部下载的进度和取消按钮，完整下载后复用正常导入流程；下载或导入失败会提示。
协议不能执行任意 Action、脚本或打开任意本地路径，也不复用 Journal 控制端口。
在电脑 B 点击链接会打开 B 上的应用；控制电脑 A 的录制/回放仍使用连接 A 的 Journal 网页工作台。

## 更新代码

以下终端示例为 Windows/Linux；macOS 将 `release` 替换为对应架构的预设。
在 VS Code 中也可通过 Git 界面更新，再对当前预设执行 Configure/Build。

```bash
git pull --ff-only
git submodule update --init --recursive
git -C ../GeoPlatform-SDK fetch origin
# 先在独立 SDK checkout 检出本次选定的完整 SHA，再执行：
git -C ../GeoPlatform-SDK lfs pull
cmake --preset release -DGPLATFORM_SDK_ROOT=../GeoPlatform-SDK
cmake --build --preset release
```

Orca 使用 App 记录的子模块指针。SDK 独立选择准确提交，不使用旧 SDK 子模块指针；缺失时检查显式 SDK_ROOT、架构及构建类型。提示 LFS 指针时在所选 SDK checkout 执行 `git lfs pull`。

## 可选扩展检查

下面以 Windows/Linux 为例；macOS 的预设和构建目录按上面的矩阵替换。

```bash
cmake --preset release -DGPLATFORM_SDK_ROOT=../GeoPlatform-SDK -DGPLATFORM_BUILD_SDK_CHECKS=ON
cmake --build --preset release --target SDKConsumerExtensionCheck
ctest --test-dir build-Release -R '^SDKConsumerExtensionCheck$' --output-on-failure
```

该检查验证应用侧自定义 DB 的注册、文档使用及渲染器工厂注册，不覆盖自定义 DB 的实际绘制。

## 脚本与验收工具

`cmake/` 负责接入 CMake；`scripts/` 包含独立 App 随包交付的构建、部署和诊断实现。
完整仓库的根目录兼容脚本转发到这些实现，SDK 的原生导出保留在完整宿主，四工作流编排统一由 App/scripts/ci 维护。
终端辅助入口为 `scripts/build.sh`（macOS/Linux）与 `scripts/build.cmd`（Windows），均使用本仓库 presets。
macOS 双架构实现为 `scripts/build_macos_universal.py`，默认目录与 preset 一致；
普通 `GPlatform_dist` 调用 `scripts/deploy_macos.py`，两者共用 `scripts/macos_bundle.py` 的部署清理和依赖审计。
Release 薄架构配置完成后，可在所选构建目录构建 `GPlatform_dist`，输出位于本仓库 `dist/`。
该目标先构建应用，再部署、临时签名、检查依赖并生成 DMG；正式发布还需要签名和公证。

截图必须通过 `scripts/auto_screenshot.sh --app /path/to/GPlatform.app` 指定本次构建的产物。
崩溃分析使用 `scripts/analyze_crash.sh`，通过 PATH、`MINIDUMP_STACKWALK` 或 `--stackwalk` 选择工具，
可用 `--symbols` 指定符号目录；工具失败会原样返回失败码。

`tests/startup/check_translations.py` 属于 App 自身检查；
`tests/startup/run_window_checks.py` 在 Windows 上运行，需要显式提供 `--application` 和 `--output`，默认使用本仓库的 Journal 工具。
模型、场景 JSON 和预期来自独立 TestAssets 仓库。启用 `GPLATFORM_BUILD_TESTS=ON` 时，CMake 在 configure 阶段将 Git/LFS 资产准备到 `tests/TestAssets` 并显示路径和完整 SHA。默认选择 macOS 的 `macmini` 或 Windows 的 `windows`，传入 `-DGPLATFORM_TEST_ASSETS_SHA=<full-sha>` 可固定版本。后续配置会离线核对并复用匹配的检出。已有目录的 HEAD 与所选 SHA 或本地记录的默认平台分支不符时，configure 停止并提示显式准备，保留用户分支和工作区。普通构建、SDK 检查和安装包构建不要求该目录。

仅运行 Journal 时，可从 App 根目录显式准备：

```sh
cmake -P cmake/thirdparty/fetch_test_assets.cmake
```

脚本模式沿用平台默认分支。可在 `-P` 前传 `-DGPLATFORM_TEST_ASSETS_SHA=<full-sha>` 固定版本。CMake 使用本机 Git 凭据，并输出实际路径和 SHA。若已有检出的版本不匹配，先保存改动，再用 Git 手动选定版本。
共享业务验收入口为 `tools/run_acceptance.py`，Python 工具和网页工作台唯一实现位于 `tools/journal/`，
随本 App 一起更新，完整宿主和独立 App 都默认使用它。网页工作台入口为 `tools/journal/journal_server.py`；执行与文件保留在服务电脑，其他电脑通过浏览器访问。
`--journal-tools` 保留为显式开发覆盖；正常测试无需配置外部目录。
目录、输入契约、统一报告和通过规则见 [应用验收架构](docs/acceptance.md)。

Zed 用户可将 `zed-template/` 复制为本地 `.zed/`。当前模板提供 macOS/Linux Configure、Build，
共用 `scripts/build.sh`；从编辑器环境设置 `Qt6_DIR`，可用 `GPLATFORM_PRESET` 选择预设，默认本机架构 Release。
将两个任务 args 中的 GPLATFORM_SDK_ROOT 占位符替换为所选 SDK checkout 或导出包 SDK 目录的绝对路径；首次 Configure 也必须明确提供，不能依赖旧缓存。
`.zed/` 与 `.vscode/` 一样默认忽略；复制模板前保留原有个人设置。

## GitHub Actions

App 仓库的 Windows / macOS 自动构建、手动选项与分支菜单规则见 [CI 使用说明](docs/ci.md)。

## 模型文件导出与工程保存

普通工作区的 File 菜单和模型右键菜单共用模型导出流程，支持选中模型／当前盘的 STL、可编辑 3MF，以及多选模型分别导出 STL。右键菜单按空白、单选、多选、修改体区分上下文：导入只在空白菜单出现，打开工程留在顶部 File 菜单；导出格式直接列为菜单项，不再套“文件／导出”子菜单。修改体的菜单明确导出其所属完整模型。STL 沿用 Orca 的正负体积布尔处理；布尔失败时提示仅导出正体积，不把修改体或辅助 Actor 当作实体。

保存工程写出全部模型和打印盘，包括未分配打印盘的实例；另存为成功后才更新保存路径，再次保存复用该路径。写入先生成同目录临时文件，失败或取消保留已有文件。分别导出 STL 使用模型名和实例 ID 命名，拒绝覆盖同名文件；部分失败时 Action 结果中的 `files` 列出已经成功的文件。

3MF 主体使用 Orca 原生工程写入器，保留部件角色、局部参数、实例变换、耗材配置及涂色。应用特有的纤维区域编辑数据、实例名称和耗材继承方式存入 `Metadata/GPlatform/model-editing.json`（版本 1），由本应用恢复；不识别该扩展的第三方程序只读取原生工程内容。扩展缺失／过期不阻止原生工程读取，诊断会说明扩展未恢复。切片页的 G-code／gcode.3mf 导出仍用于打印任务，与可编辑工程保存分别处理。

原生应用回归为 `ModelFileExportCheck`，UI 场景为 TestAssets 的 `model_file_export`。前者验证导出内容及往返，后者通过真实右键菜单、工具栏和文件选择器验证接线，并比较模型 DB 未被文件操作修改。
