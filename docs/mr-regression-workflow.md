# 多仓分支与 MR 交付

源码变更使用各仓库同名功能分支。从子仓到父仓依次提交、推送，并在父仓记录子仓的完整 gitlink SHA。推送父仓前核对子提交在远端可获取。各仓 PR 指向各自维护的集成分支。

App MR 自动回归入口、独立的 SDK 生产和发布、手动回归以及安装包流程由 [App 平台 CI](ci.md) 维护。App MR 固定 App head，宿主版本独立解析并固定。宿主候选需要单独验收时，从 App 的 Run Regression 指定准确的 `host_ref` 和 `app_ref`。

平台回归分别固定 TestAssets 的 `macmini`、`windows`。功能分支的测试夹具随候选提交保留。MR 将源分支名传给回归用于 SDK 选择。SDK 发布使用选定目标分支，独立于源码候选引用。

确认回归结果对应当前候选 SHA、覆盖所有要求的场景，并核对审查与分支保护。提交、推送、发起 PR、合并和发布分别按已授权范围执行。完整宿主的交付要求见 `docs/Development/Task_Execution_Workflow.md`。
