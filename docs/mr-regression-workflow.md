# 主仓 MR、平台回归与 SDK 更新

本文件约定本批交付顺序。实际运行仍须核对 workflow、固定输入与逐项证据；文档不代表线上门禁或回归已经通过。完整宿主的统一规范见 `docs/Development/Task_Execution_Workflow.md`。

## 功能分支与提交

- App、GPlatform 及实际修改的源码库使用同名功能分支；父仓通过完整 gitlink SHA 固定子仓，不用分支名代替依赖固定。
- TestAssets 的本批新增或修改资源推送到同名临时分支，本批不要求单独创建 TestAssets MR，也不直接写入 `main`、`macmini` 或 `windows`。
- 主仓 MR 是本批完整回归的唯一自动入口。关联 App／源码库 MR 用于各自审查，不各跑一套重复的完整应用回归。
- 提交 MR 不以先生产、提交或推送新 SDK 为前置条件。匹配 SDK 由 workflow 在源码回归通过后处理。
- 提交、推送、创建 MR、合并、发布和删除分支仍按用户授权分别执行；不自动合并或发布 Release。

## 输入与平台资产

产品源码继续使用已有分支／完整 SHA 选择能力：手动入口保留其现有参数；MR 自动入口使用主仓 MR head 的完整 SHA。App、Orca、SmoothUI 等源码从该提交的 gitlink 派生，不分别追同名分支的移动 head。

平台 Journal 回归固定使用 TestAssets 仓库的以下分支，prepare 一次解析成完整 SHA，整轮不再更新：

| 平台 | 测试资源分支 |
| --- | --- |
| Mac mini | `macmini` |
| Windows | `windows` |

不增加新的测试资源分支选择参数，不用候选 App 的 TestAssets gitlink 覆盖 Mac mini 的 `macmini` 基准。本批临时测试分支和既有平台回归资产是两个角色；不要求平台分支预先包含新场景，也不要求新场景先在目标平台通过，才允许提交 MR。

## 执行顺序

1. MR 控制器固定主仓、App 和两端平台资产 SHA，保留受信控制器的准确版本。
2. Mac、Windows 使用正式 CMake presets，从完整宿主源码构建实际被测程序，标准目标为 `gplatform_build`。此路径不消费 App 仓库中的预编译 SDK。
3. 复用候选 App 的平台 CI 回归函数和 `App/tools/run_acceptance.py --cases all`，运行各自固定平台资产中的全部活动 Journal。保留桌面、部署资源、DB 和截图判定合同。
4. 核对计划清单、完成清单、逐例结果和证据。空集合、缺项、部分执行、失败、取消、跳过、`pending` 和 `not_verified` 均不能通过。
5. 两端回归及证据上传都成功，才执行现有 SDK 更新 workflow；源码 SHA 与回归候选完全相同，复用正式构建缓存和导出入口。
6. SDK 写入同名临时分支。Mac 更新两个薄架构槽位，Windows 更新 AMD64 槽位；同一分支顺序提交，并以准确旧 SHA 校验每次更新，避免覆盖。
7. 汇总源码回归、SDK 更新及最终 SDK SHA，分别报告各阶段结果。

SDK 来源、清单、载荷和 UI 包校验保持有效。SDK 更新失败不改变已确认的源码回归事实，也不能把整次交付标为成功；定位后只处理对应 SDK 交付阶段，不为发布重跑业务回归。远端排障遵循 `AGENTS.md`，通过 SSH 检查，不用 workflow 下发诊断命令。

SDK workflow 不自动向当前 App／主仓 MR 回写依赖指针，不循环触发同一轮回归。新 SDK SHA 作为本次交付结果输出；后续独立 App 消费及其指针更新按对应交付任务处理。

## 现有入口边界

- 独立 App 的 `build-app.yml` 继续消费其固定 SDK；SDK 校验、手动源码选择、打包和显式跳过 Journal 的能力保留。
- 主仓手动回归观察入口保持原有职责，本次不迁移其旧原生回归器，也不调整算法或测试标准。
- 本条 MR 自动链路验证完整宿主构建及双平台全部平台 Journal，不宣称替代未执行的 Root 原生／算法全量、独立 App SDK 消费或安装器实装验收。
- workflow 成功不自动等于强制合并门禁已部署。准确 head 的结果关联、目标分支保护及审查状态需要分别核实；旧 head 的通过结果不能放行新候选。
