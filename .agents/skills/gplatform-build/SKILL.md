---
name: gplatform-build
description: 配置、编译、运行或排查 GPlatform 构建。先核对现有 preset、共享 recipe 和有效缓存，复用正式构建/部署入口，按实际依赖问题做必要修正。
---

# 构建与运行

先确定当前是完整宿主、独立 App，还是 Orca 单独构建。`APP_ROOT` 是本技能所属 App 根目录；仅实际存在 `Core/`、`App/`、`cmake/sdk/` 的宿主才称 `HOST_ROOT`。

## 先检查实际输入

- 检查所选源码工作区、未提交变更、CMake 版本及 `CMakePresets.json`；已有 build tree 从 `CMakeCache.txt` 核对源目录、配置、架构、Qt、SDK 和 libslicer 来源。
- `cmake --list-presets` 只列入口；configure 会准备、构建部分依赖，不能将 configure 当作无副作用的环境探测。
- 显式 CMake/cache 值优先于环境默认值；更换 Qt 或源码目录时，不假定环境变量覆盖旧缓存。

## 复用入口与范围

先明确本次要得到的目标、架构/配置、输出位置和是否需要运行/测试/打包，再选已有 preset 与目标；普通编译不默认增加发布、SDK 导出或完整长测试流程。
优先复用匹配当前源码/ABI 的已有 build tree 和依赖缓存。确认来源冲突或确需隔离时才新建目录，不把“每次全量重建”当作排错方法。
先读 `CMakePresets.json`、实际消费的共享 recipe 和现有 `scripts/build.*`。脚本是原生 configure/build 的兼容入口，不复制一份新的平台构建流程。
个人机器路径使用现有本机配置或标准输入；共享逻辑确有缺陷时在其所有者处修复并检查相关生产/消费模式，不在各入口追加不同的环境补丁。

## 按模式构建

1. macOS 选择 `arm_release/arm_debug`、`x86_release/x86_debug` 或 `universal_release/universal_debug`；Windows/Linux 选择 `release/debug`，以当前 presets 的条件为准。
2. 在所选源码根目录执行 `cmake --preset <preset>`，再执行 `cmake --build --preset <preset>`。并发数按当前资源和用户要求选择，不固定在技能中。
3. 完整宿主标准目标是 `gplatform_build`，包含应用与 SDK 导出所需产物；独立 App 标准目标是 `GPlatform`。
4. Orca 单独构建改读 `ThirdParty/OrcaSlicer/AGENTS.md` 和其 presets，不能使用宿主 preset 名。

详细输入、运行库和测试选择见 [构建模式与检查](references/build-modes.md)。

## 运行和验证

从实际 build tree/CMake 目标定位可执行文件；主仓库 App 的 binary 子目录与独立 App 不同，不能只按历史路径猜测。macOS `.app` 的命令行执行文件在 `Contents/MacOS/`。
普通启动按用户请求执行；请求真实流程验收时使用 `gplatform-journal` 的正式入口，不把程序启动成功当作功能通过。
完整宿主测试使用 `GPLATFORM_BUILD_TESTS`，独立 App 的 SDK 检查使用 `GPLATFORM_BUILD_SDK_CHECKS`；先构建相关测试目标，再运行已注册的 CTest 子集。

## 排错和说明

分别检查配置、编译、链接、部署和运行阶段。保留具体错误，不修改依赖契约来消除诊断，也不默认清空机器级缓存。
问题涉及构建源码修复时同时应用 `gplatform-bugfix`。新增选项、脚本、下载/重试或缓存策略需由具体缺口证明必要，不能为一台机器改全局工具选择、依赖版本或其他架构配置。
复查最终命令和 diff：所选缓存/产物来源是否正确，共享入口是否被复用，是否混入无关清理或产品代码改动；成功之后不重复无依据地扩大构建与测试范围。
输出源码根目录、preset/配置/架构、产物路径、实际运行的命令与结果；缺少 SDK、工具或测试时明确该边界。
SDK 导出与独立消费验证使用 `gplatform-sdk-delivery`；不因构建成功自动提交、推送、发布或更新 gitlink。
