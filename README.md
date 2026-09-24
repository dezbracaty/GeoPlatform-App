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

SDK 路径固定为 `ThirdParty/libs/GPlatformSDK/<架构>/<构建类型>/`，当前提供 `arm64/Release/`。
版本由本仓库记录的子模块提交指针决定，无需在目录名中写版本。

## CMake 编译

当前 SDK 使用 macOS arm64、AppleClang 21、Qt 6.9.3 编译，最低支持 macOS 15。
请使用配套的 C++ 工具链与 Qt；SDK 的实际构建信息见
[arm64/Release/sdk-build.json](ThirdParty/libs/GPlatformSDK/arm64/Release/sdk-build.json)。
安装 CMake 3.24+、Ninja、Qt 6.9.3（含 Quick、QuickControls2、Quick3D、Network、Concurrent、Multimedia 等模块），
将 CMake 和 Ninja 加入 PATH。在本仓库根目录执行：

```bash
export GPLATFORM_QT_ROOT=/path/to/Qt/6.9.3/macos
cmake --preset release
cmake --build --preset release
open build-Release/GPlatform.app
```

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
如果提示 SDK 缺失，请检查子模块和构建类型；如果提示 Git LFS 指针，请在 SDK 子模块中执行 `git lfs pull`。

## 可选扩展检查

```bash
cmake --preset release -DGPLATFORM_BUILD_SDK_CHECKS=ON
cmake --build build-Release --target SDKConsumerExtensionCheck
ctest --test-dir build-Release -R '^SDKConsumerExtensionCheck$' --output-on-failure
```

该检查验证应用侧自定义 DB 的注册、文档使用及渲染器工厂注册，不覆盖自定义 DB 的实际绘制。
