---
name: gplatform-mr-delivery
description: 处理 GPlatform、App、TestAssets 及相关子仓库的同名临时分支交付、提交推送、关联 MR 与回归门禁设计。用于此项目的跨仓交付，区分候选源码、本批新增测试和既有平台回归资产。
---

# 多仓临时分支与 MR 交付

先读取 `APP_ROOT/docs/mr-regression-workflow.md`（从本文件为 `../../../docs/mr-regression-workflow.md`），它保存完整约定、主入口四参数及设计／实现边界。不要把文档中的待实施门禁当成已存在能力。实际入口以当前源码和远端受信控制版本为准。

## 交付决策

- 核对每个仓库的 HEAD、分支、未提交内容、gitlink 和本次授权；保留已有改动，不擅自 pull／merge、升级依赖或强推。
- 本批 App、GPlatform、TestAssets 使用各自同名临时分支；实际涉及 Orca 等子仓库时一起处理。用户给定名称优先，否则选一个能描述本批工作的名称并说明。不直接把本批改动提交到 main 或长期目标分支，不为无变更仓库凑空提交／空 PR。
- 新测试、模型、标准、文档与 skill 都提交到对应临时分支；PR 目标由本批约定确定，不能自行指定 main。
- 本地 commit 按子到父完成并同步本地 gitlink；只要求 commit 时，不能因子提交尚未推送而阻断父仓本地提交。获得 push 授权后再按子到父推送，父仓 push 前确认引用的远端子提交可达。依赖按 gitlink SHA 固定，不靠改 `.gitmodules` 的长期 branch 配置追临时分支。
- 严格按授权区分 commit、push、创建 MR、合并、发布、删分支。后续明确要求 push 时执行最新授权；只要求提交不能擅自 push，允许 push 也不自动开 MR 或合并。

## 回归边界

- 候选程序取本批 GPlatform 提交及其锁定的 App／Orca／SDK。本批 Test 临时分支保存待交付测试改动。
- CI 的旧功能回归使用分别冻结的既有 macmini／Windows 脚本、模型和标准；原生回归逐架构明确 catalog／基准资产 SHA。本批新增场景仅做本地必要验证，不要求先在目标机器跑过、先进入平台分支，才允许提交或进入 MR。
- 不增加平台资产包含本批 Test head、祖先关系、平台承接审核或新增场景预跑证明等前置门槛。不能为了执行旧回归而自动把新场景送 main。
- 主入口契约只有 `source_ref`、`test_assets_ref`、`windows_test_assets_ref`、`platform` 四个对外参数。ref 接受分支或完整 SHA，prepare 一次解析固定；MR 强制全部平台、两平台 Journal、禁止 Release。产品依赖从父提交派生，不另加 App／Orca／SDK 覆盖参数。
- 复用现有构建／回归／打包入口，同一固定组合不因多仓 PR 重跑多遍。SDK 按生产输入和来源契约需要更新；Orca 变化也可能需要匹配 SDK，不把判断缩成仅 ABI，也不无条件每批重产。
- 全量失败阻止合并，允许继续提交修复。不能删旧用例、放宽标准、自动刷基准或用 `not_verified` 冒充通过。手动跳过回归的构建不能作为 MR 全量凭据。

## 执行和报告

只提交／推送时不顺手实现 workflow、配置保护规则或触发长回归。修改流程时复用现有控制链，补真实缺口；正式构建使用 `gplatform-build` 约定，SDK 生产／消费使用 `gplatform-sdk-delivery`，真实 Journal 使用 `gplatform-journal`。

MR 的准确 SHA 状态绑定与变更失效、逐例证据、依赖顺序合并和子提交可达性按文档核对；不承诺跨仓原子合并。技能不新增一套状态库、CI 平台或场景执行器。

交付时列明各仓分支和提交号、push 是否成功、父子引用是否一致、实际验收范围。区分方案审核、源码检查、本地必要验收、真正跨平台全量和实机打印。用户要求 agent 审核时，按 `gplatform-bugfix` 的审核规则复核最终版本，不能沿用已被用户纠正的旧结论。
