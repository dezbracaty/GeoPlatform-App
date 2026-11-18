---
name: gplatform-mr-delivery
description: GPlatform 同名功能分支、依赖 gitlink、跨仓提交推送及关联 PR 验收。
---

# 多仓分支与 MR 交付

先读 [交付约定](../../../docs/mr-regression-workflow.md) 和 [App 平台 CI](../../../docs/ci.md)。

- 核对各仓分支、源码 SHA、未提交改动、PR 目标和本次授权，保留用户已有改动。
- 实际修改的源码库使用同名功能分支。按子到父提交及推送，用完整 gitlink SHA 固定依赖，确认子提交可从远端获取。
- App MR 运行双平台源码回归。宿主候选使用 App Run Regression 的 `host_ref` 和 `app_ref` 固定准确组合。测试资产按平台冻结，SDK 发布使用固定平台分支。
- 使用正式 CMake presets 和 Journal Server 入口。核对实际候选、场景集合及逐例证据，再判断检查与门禁是否通过。
- 分别报告提交推送、实际测试和合并状态。提交、推送、发起 PR、合并和发布按各自授权执行。
