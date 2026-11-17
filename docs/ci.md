# 平台构建、SDK消费与安装包

完整参数和执行契约见 Root 的 [平台工作流](https://github.com/dezbracaty/GPlatform/blob/feature/gplatform-ui/docs/Development/Platform_Workflows.md)。控制器与产品源码分别固定SHA；本文件说明App侧消费边界。

## Build Installer

App保留一个手动 `Build Installer` 入口（build-app.yml）：

| 参数 | 默认 | 含义 |
| --- | --- | --- |
| source_ref | feature/gplatform-ui | App分支或完整SHA |
| platform | windows | windows/macos/all |
| macos_sdk_ref | macos | Mac SDK已有分支或完整SHA |
| windows_sdk_ref | windows | Windows SDK已有分支或完整SHA |

prepare只解析所选平台，冻结App/SDK/控制器SHA。Windows仅调度Windows x64 job，Mac仅调度Universal job，all并行。SDK分支长期固定，不按任务创建。CI显式检出所选SDK，通过GPLATFORM_SDK_ROOT消费，继续验证format3/UIpackage1、完整manifest、平台/架构/配置、Qt精确版本、编译器/运行时及Orca源码身份。旧SDK子模块pin只是本地兼容默认，报告必须区分声明pin与实际消费SDK，禁止静默回退。

标准configure/build、SDKConsumerExtensionCheck、部署审计、真实安装包及必要启动检查不可跳过。入口不运行完整Journal、不下载TestAssets、不自动发布Release；未执行业务回归明确标注，不能满足MR门禁。实际包是否安装/启动须据实记录，不把包文件存在当安装成功。

## 回归适配

Root统一回归入口可用源码SDK包或已发布SDK，均通过相同App消费构建和验证。回归复用App现有Journal Server、场景发现、执行器、完整DB和严格整图比较；--assets必须指向准备阶段固定的本平台资产目录。Mac默认macmini，Windows默认windows，允许已有分支/完整SHA复现。缺项、pending、失败和cleanupErrors不通过。原生/切片检查与GUI结果分别报告，不硬编码历史用例数。

## 运行环境和权限

runner必须具有所选平台正式工具链与兼容缓存；所有构建先对应preset configure再build。Windows通过已核VS开发环境、显式Qt及Inno Setup7；Mac通过匹配Xcode/Qt和Universal双架构审计。只清理本任务受管staging，不改用户WIP、原回归证据或有效SDK。

Windows机器故障直接SSH或授权会话排查，禁止workflow下发诊断、重复dispatch试错。打包/回归仅需要源码依赖读权限和Actions产物上传；SDK仓库写权限只交给Root的显式SDK发布。上传额度失败单独报告，不反复重编或擅自删旧附件。

## 合同检查

保持现有tests/ci与tests/journal入口。必要覆盖包括平台选择、冻结ref、真实SDK_ROOT、完整载荷拒绝、未选平台零解析、无测试资产的package-only模式、默认兼容、取消/清理，以及原生构建/测试失败不能用旧程序继续。GitHub正式运行前先完成本地合同检查和范围审核。
