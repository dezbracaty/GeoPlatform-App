# App 平台 CI：SDK、回归、安装包与 MR

本文件是平台 CI 的唯一完整维护规范，替代原 Root `docs/Development/Platform_Workflows.md` 中的入口设计。最终只有四个 workflow，全部维护并运行于 `dezbracaty/GeoPlatform-App`。三个手动入口互相独立，一个 App MR 自动入口复用同一回归实现；不维护额外 controller 分支。

本文描述用户已确认的最终契约。迁移中的代码、默认分支及 GitHub 已登记入口是否已切换，必须以实际提交和正式运行结果另行核实，不能把文档存在视为已上线或验收通过。

## 入口清单

| Workflow | 文件 | 触发方式 | 职责 |
| --- | --- | --- | --- |
| Build SDK | `.github/workflows/build-sdk.yml` | 手动 | 正式生产、检查、可选发布平台 SDK |
| Run Regression | `.github/workflows/run-regression.yml` | 手动、workflow_call | 完整宿主原生检查、SDK 消费检查、Server 全量回归 |
| Build Installer | `.github/workflows/build-installer.yml` | 手动 | 检查、构建、部署、打包、必要启动验证和上传 |
| MR Checks | `.github/workflows/pr-checks.yml` | App MR 自动触发 | 固定 MR head，强制双平台源码模式完整回归 |

回归摘要、SDK 附件和安装包附件在 App 仓库 Actions 查看。GPlatform 保留完整宿主源码、标准 presets、原生测试、SDK 导出入口；SDK/TestAssets 仓库保留平台产物和测试数据。

## 固定分支与候选身份

| 平台 | SDK 发布分支 | SDK Release 槽位 | TestAssets 默认分支 |
| --- | --- | --- | --- |
| windows | windows | AMD64/Release | windows |
| macos | macos | arm64/Release、x86_64/Release | macmini |

新增 Journal 场景按 [跨平台场景迁移](acceptance.md#跨平台场景迁移) 执行，先在 `macmini` 新增和验收，
再按场景迁入 `windows`。回归分别以两平台实际冻结的资产 SHA 和场景清单核对覆盖。

SDK 长期使用这两个固定分支，不创建任务 SDK 分支。源码按正常功能分支开发；所有分支在 prepare 一次解析为完整 SHA，后续不重新解析分支头。`platform=all` 两平台独立并行，fail-fast=false；Mac 自身两个薄架构遵循现有标准构建顺序。

App 是被测代码的主身份：先冻结 `app_ref`，再冻结 `host_ref`。完整宿主按选定 Root SHA 检出，`App/` 明确检出本轮 App SHA，不能默认回到 Root 旧 App gitlink。报告同时记录宿主原声明 App gitlink 和实际选定 App SHA；Root 自身源码、App 自身源码及各自依赖分别严格校验。

现有 SDK format 3 已分别记录 SOURCE_STATE 与 APP_SOURCE_STATE；继续保留真实 producer Root/App HEAD、干净状态、源码 diff 摘要、fork 来源、UI 包与完整 manifest。不能伪造来源、允许任意脏修改、取消输入一致性检查或靠循环更新 pin 生产 SDK。显式选择 App 是受控候选输入，不等于修改 Root 的源码提交或自动提交新 gitlink。

运行记录必须包含 workflow 实现 SHA、Root/App SHA、SDK 来源及实际 SDK SHA/文件包摘要、producer Root/App 身份、所选 TestAssets SHA、工具链、应用哈希、实际加载资源及报告路径。CI 编排、构建适配和启动包装器始终来自本轮固定的 workflow 实现检出，不能因为 app_ref 指向历史产品就退回旧版CI脚本；Journal 客户端/Server、业务场景和源码契约仍来自被测 App。MR 工作流实现与被测 MR head 分开固定；来自外部 fork 的代码不自动获得内部 runner 或凭据。

## Build SDK

| 参数 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| app_ref | 分支/完整 SHA | feature/gplatform-ui | 参与生产的 App 源码 |
| host_ref | 分支/完整 SHA | feature/gplatform-ui | GPlatform 完整宿主源码 |
| platform | windows / macos / all | windows | 仅调度所选平台 |
| mode | artifacts / publish | artifacts | 上传完整 SDK 包，或发布到固定平台分支并上传发布摘要 |

执行：冻结 App/宿主/依赖 → 检出准确组合 → 工具链和源码检查 → 正式 preset configure/build → `cmake/sdk/publish_sdk.py` 导出 → 完整来源、manifest、槽位、LFS/ABI 检查 → 可选 CAS 发布 → 上传证据。

发布目标始终为 SDK 的 windows/macos；prepare 分别冻结平台分支 expected-base，每个平台只更新自身槽位。Windows 不等待 Mac，不向另一平台分支合槽。Mac 两槽必须来自同一候选和一致契约。

构建、导出校验、SDK 仓库发布、Actions 上传分别记录。`artifacts` 上传完整 SDK 包、回执和清单。`publish` 的二进制交付位于对应平台 SDK 分支，Actions 只上传 `source-receipt.json` 和 `package-manifest.json`。上传失败不撤销已完成发布，也不据此重新编译。此入口不修改 App 源码、不发布 GitHub Release、不自动启动回归或 installer。

## Run Regression

| 参数 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| app_ref | 分支/完整 SHA | feature/gplatform-ui | 被测 App |
| host_ref | 分支/完整 SHA | feature/gplatform-ui | 完整宿主 |
| platform | windows / macos / all | all | 所选平台 |
| sdk_source | source / published | source | 本轮源码 SDK 包，或已发布 SDK |
| macos_sdk_ref | 分支/完整 SHA，可空 | 空 | published 时空值选 macos |
| windows_sdk_ref | 分支/完整 SHA，可空 | 空 | published 时空值选 windows |
| macos_test_assets_ref | 分支/完整 SHA | macmini | Mac 测试资产 |
| windows_test_assets_ref | 分支/完整 SHA | windows | Windows 测试资产 |

source 模式禁止填写 SDK refs，按本轮 Root/App 候选生产 SDK 文件包，仅供本轮使用，不发布 SDK 分支。published 模式使用冻结的 SDK 提交，并核对实际来源与候选契约；不兼容或来源不符必须明确失败，不回退旧 SDK。未选择的平台不解析其 SDK/资产，不调度其 runner。

完整执行：冻结版本 → 准备本平台 TestAssets → 宿主原生 CTest/原生切片检查 → 生成或获取 SDK → 构建 SDK 消费 App → 必需 SDK 检查及实际部署/资源检查 → 启动 Journal Web Server → 使用该 Server 的 /api/run 创建全量任务 → 严格验收 → 最多两轮串行复查，第二轮只复查仍失败项 → 汇总。

两平台的 SDK 生产和消费构建将编译明细写入 Runner 本机日志，Actions 记录阶段起止并在失败时显示日志尾部。Journal 在阶段或状态变化时输出汇总，同一阶段每 60 秒输出一次进度。场景及步骤明细保存在 Runner 本机，附件在任务结束时一次上传回归总报告和 Journal 摘要。上传失败在该步骤留痕，平台验收仍按实际回归结果判定。

宿主回归显式开启 `GPLATFORM_BUILD_ORCA_NATIVE_PROBES`。原生探针与运行库在同一个 Orca 构建目录中链接同一份核心归档。支撑重叠和微填充由宿主 CTest 执行一次，后续 Orca 来源核验复用同批清单、日志和 JUnit。两个检查使用 CTest 的统一默认时限。普通 preset 和 SDK 生产不启用探针目标。

原生 configure 显式使用已预检依赖的 Python3_EXECUTABLE，并打开既有 JOURNALSYSTEM_BUILD_TESTS。原生与 GUI 使用同一冻结资产；SDK 生产/消费前恢复 App 自己声明的测试资产 gitlink，不能把平台资产选择当作脏产品源码。GUI 再选择冻结资产。

共享 discover_scenarios 决定活动 ID，计划与实际集合必须一致。保留初轮、串行复查、最终身份和计数；缺项、pending、失败、执行中断、cleanupError 均不能通过。截图、DB、输出及取消语义保持现有严格契约，不修改标准制造通过。

回归入口不发布 SDK，不生成 installer，不发布 Release。原生检查、SDK 消费、应用启动、真实 Journal、输出文件检查分别报告，不互相冒充。

## Build Installer

| 参数 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| app_ref | 分支/完整 SHA | feature/gplatform-ui | 要打包的 App |
| host_ref | 分支/完整 SHA | feature/gplatform-ui | 生产本轮 SDK 的完整宿主 |
| platform | windows / macos / all | windows | 所选平台 |

执行：冻结宿主/App → 各平台按正式 preset 生产并导出本轮 SDK → 校验 SDK 包、生产源码 SHA、UI 输入和 Qt/编译器/ABI → 使用该包的 GPLATFORM_SDK_ROOT 配置并构建独立 App → SDK 消费检查 → 正式部署与 installer 目标 → 包内容、架构、依赖、哈希及必要启动检查 → 上传安装包与报告。

Windows 生成 AMD64 `.exe` installer，Mac 生成 Universal `.dmg`，all 并行。两平台分别生产临时 SDK，均使用同一组宿主/App SHA。此入口不访问 SDK 分支、不发布 SDK 或 GitHub Release、不下载 TestAssets、不跑全量 Journal。打包通过不满足 MR 全量门禁。

安装包附件包含 `build-result.json`，以及通过校验时 `package/` 下的安装包和 SHA256 文件。构建或检查失败时只上传已生成的结果 JSON。启动截图、打包诊断文件和完整检查记录留在 Runner 的 `installer-results/<run>-<attempt>/<platform>`，根路径与回归报告一致。只有合格包上传成功才清理本轮的 `package/`；构建、检查或上传失败会保留已有文件。

正式交付填写已验收候选的完整宿主/App SHA。报告记录 SDK 生产源码和文件包清单 SHA256。构建、部署和检查使用同一显式 GPLATFORM_SDK_ROOT；产物缺失、来源不一致或不兼容直接失败。

检查范围：Mac 挂载实际 DMG，核对包内完整载荷与双架构依赖并启动 App；Windows 核对正式 Inno 输入、AMD64 发行目录和载荷哈希并启动发行 App。未执行的 Windows 实装、注册表、升级卸载必须明确标注未验证，不能把发行 App 启动称为实装通过。

## MR Checks

App MR 创建、重新打开、提交更新、转为可审核、变更目标分支触发；不限制目标分支，只改标题/描述不重复执行。草稿暂不启动完整回归；外部 fork 不自动使用内部 runner/凭据，明确显示未执行，不登记为完整验收通过。

没有手动参数。固定当前 MR head 为 App SHA，宿主从仓库变量 GPLATFORM_HOST_REF 解析，空值默认 feature/gplatform-ui；测试资产默认 macmini/windows。强制 platform=all、sdk_source=source，直接调用同仓库 run-regression.yml 的 workflow_call，不能从调用方注入跳过必需检查参数。工作流实现使用审核过的固定版本，被测源码使用准确 MR head，两种身份都报告。

新提交只取消本 MR 的旧候选，不影响其他任务；取消负责收尾本任务 owned 进程。两平台所有必需检查完整通过才报告通过。合并保护应绑定 App MR Checks 的当前候选汇总，未核实服务端设置不能声称门禁已生效。

本四入口方案不保留 Root 自动 MR 入口：Root MR 不自动触发全量；Root 源码变化通过 App Run Regression 的 host_ref 显式验收，不把 App MR 检查冒称覆盖其他 Root MR。

## SDK 显式选择与职责边界

本地独立 App 与 CI 均显式传 `-DGPLATFORM_SDK_ROOT=<checkout-or-package/SDK>`。SDK 不再作为 App 子模块自动下载；长期分支仍是 macos/windows，运行时冻结完整 SHA。完整宿主生产 SDK 不需要该消费路径。

Universal 独立 App 的 outer configure 将同一 SDK_ROOT 传给两个薄架构；merge-only 必须核对两侧缓存实际消费同一选择。CI 构建/打包适配器来自固定 workflow 实现，使用现有 `--source-dir`/`-SourceDir` 选择产品项目，产品 presets、CMake、资源、source_inputs 与 Journal 工具来自被测版本。

Build Installer 使用本轮源码 SDK 做消费检查、打包和必要启动观察。完整 Journal 由 Run Regression 执行；安装包的启动观察沿用现有 owned 生命周期。

## 日常流程

1. 在 App 功能分支开发、提交、创建 MR。
2. MR 自动执行准确 head 的双平台源码模式完整回归；失败修复后新提交自动验收新候选。
3. 需要安装包时，手动 Build Installer，填写已验收的 App/宿主 SHA 和目标平台，核查实际包和上传报告后交付。
4. 需要向其他消费者交付 SDK 时，独立运行 Build SDK，填写已验收的 App/宿主 SHA 和目标平台，以 mode=publish 更新对应平台分支。

## 仓库配置与执行环境

App 仓库使用 GPLATFORM_HOST_REPOSITORY 指定完整宿主（默认 dezbracaty/GPlatform）；MR 的 GPLATFORM_HOST_REF 默认 feature/gplatform-ui。SDK 仓库地址只从选定 App 提交的 `cmake/sdk-repository.json` 读取，文件仅含 `repository`（GitHub owner/repo），不存版本 pin，也不增加可写仓库参数。测试资产仓库仍从选定 App 的真实子模块声明读取。缺少新 SDK 声明或不符合当前 SDK/Server/构建契约的历史 App 明确失败，不解析旧 SDK gitlink 兜底。

App runner 标签使用 GPLATFORM_WINDOWS_RUNNER_LABELS/GPLATFORM_MACOS_RUNNER_LABELS。源码依赖只读凭据 GPLATFORM_SOURCE_TOKEN、队列检查所需 GPLATFORM_WORKFLOW_TOKEN、SDK 发布凭据 GPLATFORM_PUBLISH_TOKEN 按阶段最小使用；publish token 只提供给显式发布阶段，回归/installer 不具备 SDK 写权限。

CI 工具测试依赖在 runner 准备阶段用同一 Python 安装 `tests/ci/requirements.txt`，截图验证沿用 Pillow 依赖。回归在 Journal 全量通过后再检查 CI 工具合同，工具检查失败仍阻止最终通过。runner 只复用测试输入和本机工具环境一致的通过记录，记录包含源检查提交，工具输入变化或无通过记录时重新执行。仅改变 TestAssets gitlink 不改变工具测试输入。

App runner 的完整宿主统一使用工作区内 `host-source`。宿主和嵌套独立 App 保留各自经校验的 preset 构建目录，每个依赖切换到固定 gitlink 后只清理一次。源码回归和 Installer 复用 runner 临时目录下的固定 SDK 路径 `regression-sdk`，每轮重新生成并校验来源和载荷。App 按标准 preset 增量构建。

`published` 回归的 `sdk` 检出保留 Git 和 LFS 对象，按冻结的 SDK SHA 获取所选平台槽位并执行完整校验。

回归报告保留 `regression-results/<run>-<attempt>/<platform>` 布局。Windows 报告放在 `RUNNER_TEMP` 的父目录下，保持归档路径短，并避开 Runner 在任务结束时清理的临时目录。Mac 使用工作区。成功和失败的任务只上传已经生成的 `source-regression.json` 和 Journal 会话的 `summary.json`，包含版本、各阶段结果、计数和逐场景判定。完整日志、截图和导出文件留在 Runner 本机，上传后保留报告目录。原子 JSON 替换仅对 Windows 短暂读句柄争用进行有界重试，持续占用或其他错误仍使流程失败。

两仓 runner 若在同一机器，继续共用现有机器锁，不能并发争抢同一缓存/桌面。迁移需核对 App runner 的正式工具链、宿主源码工作路径和兼容缓存；路径不一致不能手改 CMakeCache 或拼库冒充复用。编译不固定为两路并行；未显式配置 CMAKE_BUILD_PARALLEL_LEVEL 时按现有工具调度。

Windows 故障直接 SSH 或用户授权会话检查，禁止改 workflow/提交/反复 dispatch 下发诊断。全量 Journal 必须从 Server 发起并记录服务地址、6位码、计划与结果。保留用户 WIP、当前验收证据、有效 SDK 槽与兼容依赖缓存。

## 迁移与旧入口退役

先迁移和验证 App 新入口，再在已运行旧候选正常收尾后切换，不能中途换运行代码。新的手动 workflow 文件须先按仓库流程进入默认分支，GitHub 才能正式登记并提供 workflow_dispatch；运行时仍可选择其他工作流分支。参见 [GitHub 官方说明](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow)。不能只把新文件推到功能分支就宣称默认入口已上线。删除 Root 的 update-sdk.yml、run-regression.yml、run-build-app.yml、pr-tests.yml 及不再被调用的 CI 编排/重复测试；App 原 build-app.yml 替换为 build-installer.yml。

退役 Root 原有 build_windows/publish_delivery 手工 SDK-only/恢复 CLI；历史运行须以其原历史版本和状态恢复，新实现不保留旧状态兼容器。Root 原平台说明改为指向本文的索引，不保留第二套参数规范。更新 AGENTS、技能和提交/合并说明中的旧引用、controller SHA 和入口。停用 GitHub 已登记旧入口，历史运行和日志保留。没有用户授权不能合并 MR、发布 Release 或自动创建源码提交；已明确授权的交付按原范围执行。

迁移检查覆盖：仅四个 workflow、精确公开参数、平台选择零误调度、分支冻结、MR head 不被 Root 旧 gitlink 覆盖、Root/App 与所有 fork 干净来源、SDK_ROOT/manifest/LFS/ABI 一致性、两种 SDK 模式、来源拒绝、取消/清理、全量 ID 集合及初轮/复查计数。先本地验证，再固定候选实际运行 App 四入口；未执行的正式平台范围明确列为待验收，不把工具测试算作完整回归或安装包通过。
