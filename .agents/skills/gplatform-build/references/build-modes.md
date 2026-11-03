# 构建模式、运行来源与检查

App 路径相对 `APP_ROOT`；完整源码宿主用 `HOST_ROOT` 明示。

## 三种构建边界

| 模式 | 实际入口 | 主要产物/输入 |
| --- | --- | --- |
| 完整宿主 | `HOST_ROOT/CMakeLists.txt` 和 presets | 编译私有实现、源码 UI、App；标准 `gplatform_build` 包含 SDK 产物 |
| 独立 App | `CMakeLists.txt` 和 presets | 无 Foundation target 时进入 `cmake/GPlatformSDKConsumer.cmake`；编译应用与 Orca 源码，消费 SDK |
| Orca 单独 | `ThirdParty/OrcaSlicer/CMakeLists.txt` 和自身 presets | 公共 libslicer 库和按需测试；没有原版 wx GUI |

独立 App 的 SDK 使用 `<架构>/<配置>/sdk-build.json`，默认位于 `ThirdParty/libs/GPlatformSDK`；候选包通过 `GPLATFORM_SDK_ROOT` 指定。先核对真实槽位、LFS 是否已物化及元数据，不靠文件夹名推断兼容。

## 现有入口的职责

- `CMakePresets.json` 与构建目标拥有项目级构建选择；先确认它们能否直接表达本次任务。
- `cmake/GPlatformBuildInputs.cmake` 处理标准输入，`cmake/GPlatformThirdParty.cmake` 组合生产/消费共用的依赖 recipe，`cmake/GPlatformCacheContract.cmake` 检查缓存约定。
- `scripts/build.sh` 是 preset configure/build 的兼容入口；宿主 `scripts/build.sh` 转发到 App 实现并传入宿主 source-dir，不是另一份独立构建逻辑。
- `cmake/GPlatformUniversal.cmake` 调用现有 universal 脚本组合薄架构；部署使用当前 CMake 和 App 脚本入口。

先区分命令/机器选择错误与共享构建逻辑缺陷。个人路径沿既有本机配置表达；确需 recipe 修复时检查直接受影响的生产与消费模式。避免复制 recipe、新建另一套 PATH/Qt 探测、手工拼链接命令，或为一次排错加入永久全局开关。
有效缓存可以增量复用；必须更换源目录/ABI 时选择匹配 build tree 或新的隔离目录。已定位为某个 cache 的失配只处理该范围，不同时删掉其他架构与机器级依赖缓存。

## 选 preset 与环境

当前 macOS 薄架构 build tree 为 `build-arm64-Release/Debug`、`build-x86_64-Release/Debug`，universal 为 `build-universal-Release/Debug`；其他平台 native 为 `build-Release/Debug`。这些来自 presets，实际使用时重新核对。
`cmake/GPlatformBuildInputs.cmake` 在 project 前处理输入；Windows 必须提供包含 `Qt6Config.cmake` 的 `Qt6_DIR`，并使用 MSVC 与匹配 Qt。macOS/Linux 使用标准 Qt 查找输入；不要写死个人安装路径。
cache/命令行值优先，环境只初始化未定义项。Windows 诊断缺 Qt、工具链不匹配和 SDK 契约时保留当前选择，不静默切换另一套安装。
Ninja、Python、Git/LFS 与各平台工具按当前构建诊断检查；依赖缓存由 CMake recipe 管理，不无条件删除机器级缓存。

## libslicer 与增量来源

`cmake/thirdparty/fetch_libslicer.cmake` 根据源码路径、架构、编译器/ABI 和配置建立有锁的稳定缓存，configure 时执行独立配置、构建和 install。源码依赖参与重新配置。
在选定 cache 查 `CMAKE_HOME_DIRECTORY`、`CMAKE_BUILD_TYPE`、`Qt6_DIR`、`GPLATFORM_SDK_ROOT`、`GPLATFORM_LIBSLICER_CACHE_ROOT`、`GPLATFORM_LIBSLICER_INSTALL_DIR`、`GPLATFORM_LIBSLICER_PACKAGE_DIR`、`libslicer_DIR` 等实际存在的项。
宿主使用的 libslicer 配置可能由 `LIBSLICER_BUILD_TYPE` 和平台依赖约束决定，不根据主应用 Debug/Release 自行猜测。

## 找到实际运行产物

`CMakeLists.txt` 从 `CMAKE_CURRENT_BINARY_DIR` 推导应用路径。宿主将 App 加入自己的 binary 子目录，独立 App 则从自己的根构建，输出不能混用。
macOS 确认 `.app/Contents/MacOS/GPlatform`、Frameworks、插件/QML 和资源；Windows 确认可执行文件目录与部署 DLL。必要时使用 `otool -L`、`ldd` 或 Windows 工具检查动态加载来源，并比较相关文件哈希。
universal 由 `cmake/GPlatformUniversal.cmake` 与 `scripts/build_macos_universal.py` 组合两个薄架构并检查；它不是把多架构值塞进每个依赖 cache。SDK 按薄架构分别生产。

## 按当前模式选择测试

- 完整宿主：`GPLATFORM_BUILD_TESTS=ON`，目标和测试注册在 `HOST_ROOT/Core/tests/CMakeLists.txt` 及各子模块。
- 独立 App：`GPLATFORM_BUILD_SDK_CHECKS=ON`，当前提供 `FileImportFormatCheck`、`StartupAdmissionCheck`、`SDKConsumerExtensionCheck`、`PrimitivePickCheck`。
- Orca：自身 `LIBSLICER_BUILD_TESTS` 与 Catch2/CTest 目标。

先启用对应选项并构建有关 executable target，再用 `ctest --test-dir <build-dir> -N` 查实际测试，使用 `-R` 执行相关子集。测试名可能与 executable 不同。
QML/offscreen、真实 OpenGL、完整窗口与 Journal 验收各有范围。测试缺失时确认模式/选项/依赖，不用空 CTest 列表或“没有失败”冒充通过。
