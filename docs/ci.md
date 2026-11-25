# App 平台 CI：SDK、回归、安装包与 MR

本文件是平台 CI 的唯一完整维护规范，替代原 Root `docs/Development/Platform_Workflows.md` 中的入口设计。最终只有四个 workflow，全部维护并运行于 `dezbracaty/GeoPlatform-App`。三个手动入口互相独立，一个 App MR 自动入口复用同一回归实现；不维护额外 controller 分支。

本文描述用户已确认的最终契约。迁移中的代码、默认分支及 GitHub 已登记入口是否已切换，必须以实际提交和正式运行结果另行核实，不能把文档存在视为已上线或验收通过。

## 入口清单

| Workflow | 文件 | 触发方式 | 职责 |
| --- | --- | --- | --- |
| Build SDK | `.github/workflows/build-sdk.yml` | 手动 | 生产、校验和上传完整平台 SDK 工件 |
| Run Regression | `.github/workflows/run-regression.yml` | 手动、workflow_call | 完整宿主原生检查、SDK 消费检查、Server 全量回归 |
| Build Installer | `.github/workflows/build-installer.yml` | 手动 | 检查、构建、部署、打包、必要启动验证和上传 |
| MR Checks | `.github/workflows/pr-checks.yml` | App MR 自动触发 | 固定 MR head，使用已发布 SDK 执行双平台完整回归 |

回归摘要、SDK 附件和安装包附件在 App 仓库 Actions 查看。GPlatform 保留完整宿主源码、标准 presets、原生测试、SDK 导出入口；SDK/TestAssets 仓库保留平台产物和测试数据。

## 平台默认分支与候选身份

| 平台 | SDK 回退分支 | SDK Release 槽位 | TestAssets 默认分支 |
| --- | --- | --- | --- |
| windows | windows | AMD64/Release | windows |
| macos | macos | arm64/Release、x86_64/Release | macmini |

新增 Journal 场景按 [跨平台场景迁移](acceptance.md#跨平台场景迁移) 执行，先在 `macmini` 新增和验收，
再按场景迁入 `windows`。回归分别以两平台实际冻结的资产 SHA 和场景清单核对覆盖。

Build Installer 和 Run Regression 只解析所选平台。显式 SDK ref 优先，接受分支或完整 SHA。未填写时查找与 App 同名的 SDK 分支。仅在确认该分支不存在或 App 只有 SHA 且无分支名时，使用表中的平台默认分支。显式 ref 不存在、选中 SDK 缺槽或不兼容均直接报错。各仓库 ref 在 prepare 冻结为完整 SHA，构建和报告使用实际检出的 SHA。`platform=all` 两平台独立并行，fail-fast=false；Mac 自身两个薄架构遵循现有标准构建顺序。

App 是被测代码的主身份。Build SDK 冻结 App 和 Root，在宿主的 `App/` 检出选定 App。Run Regression 的原生检查使用选定 Root 及其固定 App，Journal 使用独立检出的选定 App。报告分别记录 Root 的 App gitlink 和选定 App SHA。Build Installer 只检出独立 App 和选定 SDK。

现有 SDK format 3 已分别记录 SOURCE_STATE 与 APP_SOURCE_STATE；继续保留真实 producer Root/App HEAD、干净状态、源码 diff 摘要、fork 来源、UI 包与完整 manifest。消费检查保留真实生产来源，以及包完整性、平台、架构、工具链、Qt、依赖和 ABI 校验。选定 SDK 的 producer Root/App SHA 可以与消费候选不同。libslicer 继续从选定 App 固定的 OrcaSlicer 源码编译和部署。

运行记录必须包含 workflow 实现 SHA、Root/App SHA、SDK 来源及实际 SDK SHA/文件包摘要、producer Root/App 身份、所选 TestAssets SHA、工具链、应用哈希、实际加载资源及报告路径。CI 编排、构建适配和启动包装器始终来自本轮固定的 workflow 实现检出，不能因为 app_ref 指向历史产品就退回旧版CI脚本；Journal 客户端/Server、业务场景和源码契约仍来自被测 App。MR 工作流实现与被测 MR head 分开固定；来自外部 fork 的代码不自动获得内部 runner 或凭据。

## Build SDK

| 参数 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| app_ref | 分支/完整 SHA | feature/gplatform-ui | 参与生产的 App 源码 |
| host_ref | 分支/完整 SHA | feature/gplatform-ui | GPlatform 完整宿主源码 |
| platform | windows / macos / all | windows | 仅调度所选平台 |

Build SDK 冻结 App 和宿主，检出准确组合并使用正式 preset 构建，由 `cmake/sdk/publish_sdk.py` 导出。校验生产来源、完整清单和所选架构槽位后，上传 `SDK/`、`source-receipt.json` 和 `package-manifest.json`。

### 从本机发布

在完整宿主 checkout 中执行。确认 Root、App 和各源码 fork 已检出候选，`gh auth status` 显示具有目标 SDK 仓库写权限的本机身份。指定已成功的 Build SDK run ID、attempt、选定的完整 Root/App SHA 与要更新的目标分支。

```sh
python3 App/scripts/ci/update_sdk.py \
  --source-dir . --source-sha <root-sha> --app-sha <app-sha> \
  --run <run-id> --attempt <attempt> --platform all \
  --windows-sdk-target windows --macos-sdk-target macos \
  --result <本机回执路径>
```

`--platform` 支持 windows、macos 和 all。可以用 `--app-branch` 提供与 App 同名的默认目标，显式的 `--windows-sdk-target` 和 `--macos-sdk-target` 优先。只有 App 完整 SHA 时，必须为所选平台显式提供目标。SDK 仓库地址从 App 候选的版本化声明读取。

本机入口只接受该 run 和 attempt 的成功 Build SDK 工件，核对各平台的构建和上传步骤、工件摘要、完整包、槽位清单及实际生产来源。所有所选工件校验后再写分支。两平台指向同一目标时只更新一次。首次创建分支使用冻结的 SDK 仓库默认 HEAD，写入时验证分支仍不存在。已有分支按冻结的预期旧 SHA 更新，只替换所选槽位，保留其他槽位和 LFS 规则。LFS 对象校验与上传先于分支更新。本机回执记录工件 ID 和摘要、生产 Root/App SHA、更新槽位和实际远端 SDK SHA。

Git 仅在本次进程中使用 `gh auth git-credential` 提供的当前本机身份。Build SDK 使用现有源码读取与 runner 配置，SDK 写权限仅用于本机发布。

## Run Regression

| 参数 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| app_ref | 分支/完整 SHA | feature/gplatform-ui | 被测 App |
| host_ref | 分支/完整 SHA | feature/gplatform-ui | 完整宿主 |
| platform | windows / macos / all | all | 所选平台 |
| macos_sdk_ref | 分支/完整 SHA，可空 | 空 | 显式指定 macOS SDK，空值使用共用选择规则 |
| windows_sdk_ref | 分支/完整 SHA，可空 | 空 | 显式指定 Windows SDK，空值使用共用选择规则 |
| macos_test_assets_ref | 分支/完整 SHA | macmini | Mac 测试资产 |
| windows_test_assets_ref | 分支/完整 SHA | windows | Windows 测试资产 |


冻结所选平台的源码、SDK 和 TestAssets 后，按 SHA 获取 SDK，执行宿主原生 CTest 和原生切片检查，并构建和部署独立 App。完成 SDK 消费及运行资源检查后，通过 Journal Web Server 的 `/api/run` 创建全量任务。沿用严格业务验收、最多两轮串行复查和原有汇总，第二轮只复查仍失败项。

两平台的独立 App 消费构建将编译明细写入 Runner 本机日志，Actions 记录阶段起止并在失败时显示日志尾部。Journal 在阶段或状态变化时输出汇总，同一阶段每 60 秒输出一次进度。场景及步骤明细保存在 Runner 本机，附件在任务结束时一次上传回归总报告和 Journal 摘要。上传失败在该步骤留痕，平台验收仍按实际回归结果判定。

宿主回归显式开启 `GPLATFORM_BUILD_ORCA_NATIVE_PROBES`，按正式 preset 构建 `--target all` 后从同一构建目录执行一次 CTest。CTest 执行支撑重叠、微填充及 `original48`、`extended26`。切片探针和运行库链接同一份 Orca 核心，C++ 比较器按编译目标选择基线。清单和 JUnit 必须证明四项各执行一次并通过。逐例 G-code 差异保留在平台报告的 `orca-gcode/` 目录。普通 preset 和 SDK 生产不启用探针目标。

原生 configure 显式使用已预检依赖的 Python3_EXECUTABLE，并打开既有 JOURNALSYSTEM_BUILD_TESTS。原生与 GUI 在各自的 checkout 使用同一冻结资产。

共享 discover_scenarios 决定活动 ID，计划与实际集合必须一致。保留初轮、串行复查、最终身份和计数；缺项、pending、失败、执行中断、cleanupError 均不能通过。截图、DB、输出及取消语义保持现有严格契约，不修改标准制造通过。

回归入口不发布 SDK，不生成 installer，不发布 Release。原生检查、SDK 消费、应用启动、真实 Journal、输出文件检查分别报告，不互相冒充。

## Build Installer

| 参数 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| app_ref | 分支/完整 SHA | feature/gplatform-ui | 要打包的 App |
| macos_sdk_ref | 分支/完整 SHA，可空 | 空 | 显式指定 macOS SDK，空值使用共用选择规则 |
| windows_sdk_ref | 分支/完整 SHA，可空 | 空 | 显式指定 Windows SDK，空值使用共用选择规则 |
| platform | windows / macos / all | windows | 所选平台 |

冻结 App 和 SDK 后，校验所选 SDK 的包完整性、真实生产来源、源码输入和 Qt/编译器/ABI。通过显式 GPLATFORM_SDK_ROOT 和正式 preset 构建独立 App，执行消费检查，再经正式部署和 installer 目标生成安装包。检查实际包内容、架构、依赖、哈希及应用启动，再上传安装包与报告。

Windows 生成 AMD64 `.exe` installer，Mac 生成 Universal `.dmg`，all 并行。两平台使用同一个 App SHA 及各自冻结的 SDK。此入口的范围为 App 构建、安装包及启动检查。打包通过不满足 MR 全量门禁。

安装包附件包含 `build-result.json`，以及通过校验时 `package/` 下的安装包和 SHA256 文件。构建或检查失败时只上传已生成的结果 JSON。启动截图、打包诊断文件和完整检查记录留在 Runner 的 `installer-results/<run>-<attempt>/<platform>`，根路径与回归报告一致。只有合格包上传成功才清理本轮的 `package/`；构建、检查或上传失败会保留已有文件。

正式交付可显式填写已验收的 App 和平台 SDK 完整 SHA。报告记录实际 SDK SHA 与 SDK 中的 producer Root/App。构建、部署和检查使用同一显式 GPLATFORM_SDK_ROOT；产物缺失、来源不一致或不兼容直接失败。

检查范围：Mac 在部署时排除薄包内可执行文件旁的本机 Journal 数据，再挂载实际 DMG，核对包内完整载荷与双架构依赖并启动 App；Windows 核对正式 Inno 输入、AMD64 发行目录和载荷哈希并启动发行 App。未执行的 Windows 实装、注册表、升级卸载必须明确标注未验证，不能把发行 App 启动称为实装通过。

## MR Checks

App MR 创建、重新打开、提交更新、转为可审核、变更目标分支触发；不限制目标分支，只改标题/描述不重复执行。草稿暂不启动完整回归；外部 fork 不自动使用内部 runner/凭据，明确显示未执行，不登记为完整验收通过。

没有手动参数。固定当前 MR head 为 App SHA，宿主从仓库变量 GPLATFORM_HOST_REF 解析，空值默认 feature/gplatform-ui；测试资产默认 macmini/windows。使用 platform=all，直接调用同仓库 run-regression.yml 的 workflow_call。通过其 `app_branch` 参数传入实际 MR 源分支名，用于同名 SDK 选择，App checkout 仍使用完整 head SHA。工作流实现使用审核过的固定版本，被测源码使用准确 MR head，两种身份都报告。

新提交只取消本 MR 的旧候选，不影响其他任务；取消负责收尾本任务 owned 进程。两平台所有必需检查完整通过才报告通过。合并保护应绑定 App MR Checks 的当前候选汇总，未核实服务端设置不能声称门禁已生效。

本四入口方案不保留 Root 自动 MR 入口：Root MR 不自动触发全量；Root 源码变化通过 App Run Regression 的 host_ref 显式验收，不把 App MR 检查冒称覆盖其他 Root MR。

## SDK 显式选择与职责边界

本地独立 App 与 CI 均显式传 `-DGPLATFORM_SDK_ROOT=<checkout-or-package/SDK>`。SDK 独立检出，运行时使用完整 SHA。完整宿主生产 SDK 不需要该消费路径。

Universal 独立 App 的 outer configure 将同一 SDK_ROOT 传给两个薄架构；merge-only 必须核对两侧缓存实际消费同一选择。CI 构建/打包适配器来自固定 workflow 实现，使用现有 `--source-dir`/`-SourceDir` 选择产品项目。产品 presets、CMake、资源、source_inputs 和全量回归的 Journal 工具来自被测版本。Installer 启动观察脚本和所用 Journal 客户端来自固定的 workflow 提交。

Build Installer 使用选定的已发布 SDK 做消费检查、打包和必要启动观察。安装包启动观察在自身启动的实例中按桌面自然尺寸截图，检查图像有效性。完整 Journal 由 Run Regression 执行，继续使用固定回放窗口和比较标准。

## 日常流程

1. 在 App 功能分支开发、提交、创建 MR。
2. MR 自动对准确 App head 与选定的已发布 SDK 执行双平台完整回归。
3. 需要安装包时，手动 Build Installer，选择 App、SDK 和目标平台，核查实际包和上传报告后交付。
4. 需要发布 SDK 时，先运行 Build SDK 生成并上传工件，再从本机发布指定成功 run/attempt 的产物。

## 仓库配置与执行环境

App 仓库使用 GPLATFORM_HOST_REPOSITORY 指定完整宿主（默认 dezbracaty/GPlatform）；MR 的 GPLATFORM_HOST_REF 默认 feature/gplatform-ui。SDK 仓库地址只从选定 App 提交的 `cmake/sdk-repository.json` 读取，文件仅含 `repository`（GitHub owner/repo），不存版本 pin，也不增加可写仓库参数。测试资产仓库仍从选定 App 的真实子模块声明读取。缺少新 SDK 声明或不符合当前 SDK/Server/构建契约的历史 App 明确失败，不解析旧 SDK gitlink 兜底。

App runner 标签使用 GPLATFORM_WINDOWS_RUNNER_LABELS/GPLATFORM_MACOS_RUNNER_LABELS。Actions 使用源码依赖只读凭据 GPLATFORM_SOURCE_TOKEN 和队列检查凭据 GPLATFORM_WORKFLOW_TOKEN。SDK 发布使用本机现有 gh 身份。

CI 工具测试依赖在 runner 准备阶段用同一 Python 安装 `tests/ci/requirements.txt`，截图验证沿用 Pillow 依赖。回归在 Journal 全量通过后再检查 CI 工具合同，工具检查失败仍阻止最终通过。runner 只复用测试输入和本机工具环境一致的通过记录，记录包含源检查提交，工具输入变化或无通过记录时重新执行。仅改变 TestAssets gitlink 不改变工具测试输入。

App runner 在 `host-source` 检出完整宿主，在 `source` 检出独立 App，并保留经校验的标准 preset 构建目录。独立 App 按标准 preset 增量构建。

回归和 Installer 的 `sdk` 检出保留 Git 和 LFS 对象，按冻结的 SDK SHA 获取所选平台槽位并执行完整校验。

回归报告保留 `regression-results/<run>-<attempt>/<platform>` 布局。Windows 报告位于 `RUNNER_TEMP` 的父目录，Mac 报告位于工作区。平台通过并写入摘要后，Runner 清理本次截图、差异图、原生切片产物及导出，保留平台与 Journal 摘要。Journal 已完整结束时，最终通过的场景（包括复测恢复项）按原策略清理。

最终失败的场景按初轮和各次复测的逐图判定清理。只保留有失败判定且已生成的实际截图，以及该图在本次尝试中的基线副本、差异图和对应的审核证据（存在时）。业务失败但截图均通过的尝试保留 JSON、业务产物和非截图诊断日志。审核异常能从记录定位到具体截图时按同一规则处理。批次审核异常无法定位时保留错误摘要和批次诊断日志，已记录的截图仍按各自的判定处理。中断、未收齐摘要或仍待审核的批次保留现场。单次尝试缺少执行结果时保留该次证据。清理只使用本轮报告目录下的证据。

工作流上传平台 `source-regression.json` 和 Journal 会话 `summary.json`，包含版本、各阶段结果、计数和逐场景判定。摘要上传失败时仍可从 Runner 本机读取。原子 JSON 替换仅对 Windows 短暂读句柄争用进行有界重试，持续占用或其他错误仍使流程失败。

两仓 runner 若在同一机器，继续共用现有机器锁，不能并发争抢同一缓存/桌面。迁移需核对 App runner 的正式工具链、宿主源码工作路径和兼容缓存；路径不一致不能手改 CMakeCache 或拼库冒充复用。编译不固定为两路并行；未显式配置 CMAKE_BUILD_PARALLEL_LEVEL 时按现有工具调度。

Windows 故障直接 SSH 或用户授权会话检查，禁止改 workflow/提交/反复 dispatch 下发诊断。全量 Journal 必须从 Server 发起并记录服务地址、6位码、计划与结果。保留用户 WIP、当前验收证据、有效 SDK 槽与兼容依赖缓存。

## 迁移与旧入口退役

先迁移和验证 App 新入口，再在已运行旧候选正常收尾后切换，不能中途换运行代码。新的手动 workflow 文件须先按仓库流程进入默认分支，GitHub 才能正式登记并提供 workflow_dispatch；运行时仍可选择其他工作流分支。参见 [GitHub 官方说明](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow)。不能只把新文件推到功能分支就宣称默认入口已上线。删除 Root 的 update-sdk.yml、run-regression.yml、run-build-app.yml、pr-tests.yml 及不再被调用的 CI 编排/重复测试；App 原 build-app.yml 替换为 build-installer.yml。

退役 Root 原有 build_windows/publish_delivery 手工 SDK-only/恢复 CLI；历史运行须以其原历史版本和状态恢复，新实现不保留旧状态兼容器。Root 原平台说明改为指向本文的索引，不保留第二套参数规范。更新 AGENTS、技能和提交/合并说明中的旧引用、controller SHA 和入口。停用 GitHub 已登记旧入口，历史运行和日志保留。没有用户授权不能合并 MR、发布 Release 或自动创建源码提交；已明确授权的交付按原范围执行。

迁移检查覆盖：仅四个 workflow、精确公开参数、平台选择零误调度、分支冻结、MR head 不被 Root 旧 gitlink 覆盖、Root/App 与所有 fork 干净来源、SDK_ROOT/manifest/LFS/ABI 一致性、SDK ref 优先级、缺槽或不兼容拒绝、同目标一次发布、取消/清理、全量 ID 集合及初轮/复查计数。先本地验证，再固定候选实际运行 App 四入口；未执行的正式平台范围明确列为待验收，不把工具测试算作完整回归或安装包通过。
