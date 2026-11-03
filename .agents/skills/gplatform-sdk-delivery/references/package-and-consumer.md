# SDK 包、来源与消费检查

路径显式区分 App 与完整宿主。只有 App 时可检查候选 SDK 和消费端；生产/导出需要相应宿主源码和已有构建产物。

## 生产侧

`HOST_ROOT/cmake/sdk/GPlatformSDK.cmake` 生成 sdk-export 中的 settings、targets、UI 模块与 source-input 清单，正常构建的 `gplatform_sdk_artifacts` 依赖应用和导出库。
它记录宿主 `SOURCE_STATE`、App `APP_SOURCE_STATE` 以及 SmoothUI、SmoothUIDock、KDDockWidgets、Orca 四个 fork 的实际来源与内容。App 是真实独立 Git 仓库，宿主 gitlink dirty 不能替代其实际 diff。
`HOST_ROOT/cmake/sdk/publish_sdk.py` 只处理已有导出产物，不负责下载、编译、Git 提交、LFS 推送或发布。

## 复用生产与消费链路

先对照本次消费需求与当前公开接口、产物清单、UI contract 和来源元数据，确认缺口在构建、导出、包内容、消费选择还是运行部署。
已有导出清单、`publish_sdk.py`、App 的 `cmake/source_inputs.py` 与 `GPlatformSDKConsumer.cmake` 分别拥有对应职责；在所属阶段补必要逻辑，不在 CI/包装脚本重复维护另一份库清单、兼容矩阵或来源检查。
源码/工具链/ABI 变化后核对 settings 和 export 清单的生成时点，需要时重新配置和构建相应产物。不要把新库混入旧 provenance，或手工修改版本/摘要让旧包看起来兼容。
`publish_sdk.py` 中已有 lock、synchronize、preserve_other_sdks 与最终 package 验证；沿用它们的锁和失败恢复，不另写一套替换/拷贝流程。消费端已有运行库部署函数，局部运行问题先确认实际加载链，不通过任意全局搜索或复制 DLL 兜底。
仅需验证已有匹配包时直接检查/消费，无需重新生成无关架构；确需契约升级时完成必要 producer/consumer 配套，保留范围说明，不顺带迁移整个 SDK 布局或所有发布流程。

生产导出前核对当前工作区和 settings 来源；完整标准构建产生所需 artifacts 后执行：

```sh
python3 cmake/sdk/publish_sdk.py --export <build>/sdk-export --output <build>/package
```

输出不能覆盖源码、export 目录、已有构建输入或机器缓存。当前只支持单配置、单架构和静态 UI；universal 应分别生产两个薄架构槽。
导出带 output lock、暂存、旧包备份和失败恢复；保留经过校验的其他架构槽。遇到中断残留先读错误和状态，不直接删除不明旧包。

## 包的实际契约

当前包输出为 `SDK/<arch>/<configuration>/` 和顶层 `package-manifest.json`；槽内有 `sdk-build.json`、`sdk-manifest.json`、UI contract、库、头文件、QML/typeinfo/资源及相关工具。
SDK format 3、UI binary package 1，四个 UI URI 为 SmoothUI、SmoothUI.Controls、SmoothUI.impl、SmoothUIDock，当前 link mode 是 STATIC。Orca 是消费端唯一源码 fork。
`APP_ROOT/cmake/source_inputs.py::verify_sdk_package()` 校验完整文件清单、逐文件摘要、UI contract 摘要、模块和资源映射。单个 dylib/DLL 存在不足以说明 SDK 完整。
source provenance 包括提交、dirty/diff 摘要和 managed-file 内容；检查失败时定位具体输入，不通过改 metadata 或放松校验来掩盖不兼容。

## App 消费侧

入口是 `APP_ROOT/cmake/GPlatformSDKConsumer.cmake`，从实际编译器/系统/OSX 架构选 `<GPLATFORM_SDK_ROOT>/<arch>/<CMAKE_BUILD_TYPE>`：

- 要求单配置，核对系统、SDK format、实际库/LFS 指针和完整 package。
- 编译器 ID 必须匹配；MSVC 主次工具版本和运行时参数有明确检查。
- 读取 SDK 的依赖版本、feature/build-type 输入，显式冲突报错，provider 解析后再次核对。
- `find_package(Qt6 <sdk-version> EXACT ...)` 目前要求精确 Qt 版本，Windows 保持指定 Qt6_DIR 查找。
- `source_inputs.py` 消费模式验证 UI package 并只验证 Orca 源码输入；真实 Git checkout 可从当前 tracked files 重算，无 Git 时需要完整导出的来源清单。不要将 source-inputs.json 不存在单独解释为 checkout 必然损坏。
- runtime 部署函数跟踪 SDK 库并复制到实际应用位置；仍需核对实际运行来源。

本地验证用外置候选 SDK，保留源码 SDK pin：

```sh
cmake --preset <preset> -DGPLATFORM_SDK_ROOT=<package>/SDK -DGPLATFORM_BUILD_SDK_CHECKS=ON
cmake --build --preset <preset>
```

标准 build preset 不一定构建检查目标。按 `APP_ROOT/CMakeLists.txt` 单独构建有关 check executable，再运行 CTest；例如公开扩展与 primitive picking 检查。

## 交付证据与提交边界

记录生产源码/工作区状态、工具链、架构、配置、包摘要、消费端所选槽位、实际运行库与验证结果。
应用接口类布局、虚函数表或共享头文件变化需要配套重编 SDK；SDK 内置后端依赖部分应用 DB 契约，不保证任意新 DB 自动可绘制。
主仓库生产 SDK，App 维护 SDK gitlink，宿主维护 App gitlink，Orca 独立维护源码提交。只有用户授权的交付范围才执行相应外部操作；本技能不默认扩大到跨仓库提交与发布。
文件包导出、独立编译运行、正式 clone/submodule/LFS 获取分别验证和陈述。
