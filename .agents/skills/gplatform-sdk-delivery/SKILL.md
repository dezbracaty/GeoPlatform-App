---
name: gplatform-sdk-delivery
description: 导出、检查或验证 GPlatform SDK 与独立 App 消费。先明确交付契约和来源，复用现有生产、打包、校验与部署流程，只修改当前交付所需部分。
---

# SDK 导出与交付

用于 SDK 生产、检查与独立 App 消费。技能随 App 保存；生产和导出需要完整源码宿主的 `cmake/sdk/`，独立 App 侧可执行消费检查而不依赖私有源码。

## 先确定交付范围

区分 SDK 构建、文件包导出、独立 App 编译运行和正式仓库交付。检查实际源码/SDK 提交、未提交修改、架构、配置与构建来源；不要把父仓库 gitlink 当成 App 或依赖内部状态。

当前生产与消费实现见 [SDK 包与消费契约](references/package-and-consumer.md)。App 路径相对 `APP_ROOT`，生产路径相对实际存在的 `HOST_ROOT`。

## 先查现有契约和实现

明确交付对象、架构/配置、所需公开接口/资源、预期消费方式和本次验证边界。格式、目录、依赖元数据和 ABI 以现有 producer/consumer 定义为起点，不为一次发布先设计另一种包格式。
先查 SDK export、`publish_sdk.py`、`cmake/source_inputs.py`、`GPlatformSDKConsumer.cmake` 和现有部署/CI 调用链，确认现有能力是否已经覆盖。复用清单生成、来源检查、锁、暂存/替换及运行库部署，不另写逐文件复制、校验或发布脚本。
确有接口/资源缺口时在拥有它的导出或消费位置补齐必要契约，并验证两端；局部交付问题不默认扩大为 SDK 布局迁移、兼容框架或跨仓库历史改造。

## 执行方法

1. 生产：用匹配的薄架构 preset 构建完整宿主的标准目标，核对 `sdk-export/settings.json`、导出清单与产物来源。
2. 导出：使用宿主唯一入口 `cmake/sdk/publish_sdk.py --export <build>/sdk-export --output <build>/package`，核对包清单和校验结果。
3. 消费：独立配置 App，以 `GPLATFORM_SDK_ROOT=<package>/SDK` 指向候选包；不要为本地验证修改源码 SDK 子模块指针。
4. 验证：检查 App 真正消费候选槽位，运行相关 SDK 检查与应用场景，核对实际部署/加载的库和 QML 资源。
5. 正式交付按用户授权范围处理 SDK/LFS、App pin 和宿主 gitlink；打包流程本身不提交、推送或发布。

上述为相关阶段的入口，不要求每个检查任务重新生产全部 SDK；已有匹配且可验证的包可直接用于所请求的消费检查。

## 当前边界

- 包按架构/配置分槽；Debug 与 Release、单架构与 universal 合成流程分别核对。
- 当前格式为 SDK 3、UI binary package 1；UI 是静态二进制及相应 QML/typeinfo/资源，Orca 仍为消费端源码输入。
- 消费端实际核对平台、编译器、MSVC 主次版本/运行时、依赖参数、来源和 Qt 精确版本；不能沿用旧文档中更宽松的兼容假设。
- 类布局、虚函数或共享头文件变化需要重新生产匹配 SDK；不要仅复制一个 DLL/dylib 解决 ABI 不一致。
- 文件包验证不等于正式 clone/submodule/LFS 交付验证。保留验证范围差异。

## 结果与范围检查

核对实际包、元数据、来源和运行库是否相互匹配，说明复用入口、必要变更及真实验证。新增格式字段、兼容分支、配置或工具需有当前消费需求，不能手工改清单/版本来掩盖来源不一致。
检查是否误写源码 SDK、改动无关架构槽位、自动更新 pin，或混入不必要的部署/CI 重构。涉及缺陷修复时同时应用 `gplatform-bugfix`。
