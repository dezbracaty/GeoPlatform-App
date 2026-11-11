# App 双平台构建

跨仓临时分支提交和 MR 全量门禁的讨论定稿见 [MR 回归流程](mr-regression-workflow.md)。其中四参数主入口及强制门禁是待实施设计；下文描述目前已实现的 App 手动构建流程，二者不能混作已上线能力。

本仓库只有一个 `Build App` workflow，同时构建 Windows x64 和 macOS Universal。入口为 Actions → Build App → Run workflow；主仓库的 Run Build App 也调用同一入口并等待真实结果。

仅手动或被主仓库显式调用，不监听源码 push，不更新 SDK；默认在全部启用的检查成功后发布 App 预发布版。

## 输入和版本

| 输入 | 默认值 | 含义 |
| --- | --- | --- |
| `source_ref` | `feature/gplatform-ui` | App 已有分支或完整提交 SHA |
| `test_assets_ref` | `macmini` | TestAssets 仓库已有分支或完整提交 SHA，仅用于 macOS 回归 |
| `run_journal` | `true` | 是否执行 macOS Journal 回归；设为 `false` 仅编译、检查、部署和打包 |
| `windows_test_assets_ref` | `windows` | Windows TestAssets 分支或完整 SHA，独立于 macmini |
| `run_windows_journal` | `true` | 是否执行 Windows Journal 回归 |
| `publish_release` | `true` | 全部构建及启用的检查成功后，是否发布到本 App 仓库的 Releases；设为 `false` 只保留 Actions 附件 |
| `request_id` | 空 | 主仓库内部关联 ID；独立手动运行可留空 |

准备任务固定 App SHA，读取其中 `ThirdParty/libs/GPlatformSDK` 的 gitlink。两平台消费同一个 App SHA 和 SDK SHA，不提供“最新 SDK”或单独切换源码依赖的选项。
测试资产版本从该 App 声明的 TestAssets 仓库独立解析为固定 SHA，并记录到本次输入。
`macmini` 是测试资产分支，不要求宿主或 App 创建同名分支。选择的远端引用必须存在，否则准备失败；
本地分支不会自动发布，也不会自动成为 CI 输入。
两个平台的回归开关均为 `false` 时不解析或下载 TestAssets，不需要 Journal 工具、Pillow 或图形桌面会话；
SDK 校验、`SDKConsumerExtensionCheck`、Universal 部署审计和安装包生成仍执行。
此选项不会修改基线，也不会将未执行的 Journal 记成通过。

App 消费 SDK 格式 3 / UI package 1，产品构建依赖 SDK 与 Orca 两个引用。`tests/TestAssets` 声明 `update=none`，产品编译仍跳过它；各平台回归阶段显式初始化该仓库，再检出准备阶段选定的测试 SHA 并获取 LFS 实体，不改 App gitlink。详见 [验收架构](acceptance.md)。SmoothUI、SmoothUIDock 及其 KDDockWidgets 实现由生产者编成 SDK，App 不克隆这些实现仓库；构建前验证完整 SDK manifest、UI 资源合同及 Orca 受管内容/版本，不改写来源清单。

控制 workflow/CI 脚本来自本次 workflow 提交，产品代码来自固定的 App 提交；两者分别记录。App 仓库自己的 `.github/`、`scripts/ci/`、`tests/ci/`、`docs/ci.md` 由本仓库维护，主仓库同步产品源码时保留。

## runner 与少量配置

必须是本仓库有权调度的 runner；主仓库注册的 repo runner 不会自动接本仓库任务。同一物理机如注册两个仓库 runner，重编共享同一把 OS 文件锁。

| Variable | 示例结构 | 用途 |
| --- | --- | --- |
| `GPLATFORM_WINDOWS_RUNNER_LABELS` | `["self-hosted","Windows","X64","gplatform-build"]` | Windows runner 标签 |
| `GPLATFORM_MACOS_RUNNER_LABELS` | `["self-hosted","macOS","ARM64","gplatform-macos-build"]` | Mac runner 标签 |

示例标签不代表已注册机器。prepare 实际查询本仓库 runner，输出 online/busy 信息；无匹配在线 runner 立即失败。busy 可排队，queue_guard 最多看护 30 分钟；超时请求取消本次运行，不能把请求已接受说成已经停止。

| Secret | 权限 |
| --- | --- |
| `GPLATFORM_SOURCE_TOKEN` | App、SDK、TestAssets、私有源码子模块的 Contents 读 |
| `GPLATFORM_WORKFLOW_TOKEN` | 供 hosted prepare 查询本仓库 runner 的 Administration 读 |
| `GPLATFORM_RELEASE_TOKEN` | 本 App 仓库的 Contents 写；开发提交的 workflow 与默认分支不同时还需 Workflows 写。仅 `publish_release=true` 必需 |

平台编译 job 仅注入 SOURCE，不注入独立发布 Secret；SOURCE 应配置为只读。实际权限由配置的 Token 决定，Secret 命名不会降低权限；经明确授权采用更广权限时，验收记录必须注明，不能宣称仍满足只读隔离。queue_guard 用本仓库 `GITHUB_TOKEN` 的 Actions 写权限，只取消本次运行。发布使用独立的 hosted `publish` job，仅该任务注入 RELEASE；不扩大 SOURCE 的权限。发布开启但未配置 RELEASE 时，准备阶段立即失败。发布 job 的内置 `GITHUB_TOKEN` 只用于读取本轮 Actions 附件。凭据配置由获授权的管理身份完成。

runner 需要 Git/LFS、Python 3.12、CMake/Ninja 和相容 Qt。Windows 需要 PowerShell 7、MSVC C++/Windows SDK、Inno Setup 7；Mac 需要激活的 Xcode 和可运行 x86_64 检查的 Rosetta。固定的 Node 24 Actions 要求 runner 至少 2.327.1。

Mac GUI 回归要求 runner 运行在已登录且可用的图形用户会话中，屏幕可用逻辑区域至少容纳
1382×954 窗口；不能用 offscreen 代替实际回放。执行器只激活它自己启动且身份核对一致的 App。
runner Python 必须有 Pillow；辅助看图依赖见 `tools/requirements.txt`，OCR/辅助计算不决定通过。
Journal Python 工具固定使用所选 App 提交中的 `tools/journal/`，不再依赖 runner 的
`GPLATFORM_JOURNAL_TOOLS` 外部目录；工具哈希继续记录到回归输入。
这些工具和所选 SDK 中的 C++ JournalSystem 必须配套支持固定逻辑窗口协议；
不通过替换部署库、自动升级 SDK 或放宽截图判定绕过不匹配。

环境部署在 workflow 之外完成：安装 Python >= 3.12，并使 runner 服务的 PATH 能直接执行 python；Windows 的 PowerShell 脚本执行策略也由部署阶段配置。Mac 可将已安装 Homebrew Python 的 libexec/bin 加入 runner PATH。修改 runner 环境后，在空闲时重启对应服务并检查工具。workflow 不下载或安装工具，不改 PATH、不创建解释器别名、不设置执行策略；只读检查 Python，环境缺失直接失败。

工具路径沿用普通项目入口支持的 `Qt6_DIR` / `GPLATFORM_VS_ROOT` 等环境配置，不提交机器绝对路径。`GPLATFORM_MACHINE_BUILD_LOCK` 可指定跨仓库共享锁路径；默认用户缓存目录 `.cache/gplatform/build.lock`。`CMAKE_BUILD_PARALLEL_LEVEL` 控制编译并行度。

## 执行和附件

Windows 复用 `scripts/build.ps1`，Mac 复用薄 preset 和 `scripts/build_macos_universal.py --merge-only`，保留项目默认构建目录。

每平台必须实际构建并运行 `SDKConsumerExtensionCheck`；Mac 两架构分别检查。
Mac 合并、部署及审计 Universal App 完成后，在同一个受机器锁保护的 job 中并行执行两项：
现有 `create_disk_image` 生成 DMG；正式 `run_acceptance.py --cases all` 对该 App 执行全部场景。
DMG 帮助函数使用自己的暂存副本，不能修改被测 App。Journal 场景仍串行运行，不需要第二个 runner。
两项分别保存终态，一项失败仍等待另一项保存证据；任一失败导致 Mac job 失败。
两平台独立完成，任何检查、编译、打包、必需回归或附件上传失败均导致整体失败。
Windows 在现有 installer 目标完成后，串行调用同一个 `tools/run_acceptance.py --cases all`，
对 `build-Release/GPlatform.exe` 回归。安装目标可能更新部署产物，因此不与回放并发。
Windows 固定独立的资产 SHA，核对实际 DPR=1、窗口及截图 1382×954；runner 必须处于
已登录且解锁的桌面会话，Session 0 服务会话直接失败。App 显式配置专用测试子进程，系统桌面缩放不变。
Windows 同样单独上传回归证据，失败仍上传；`build-result.json` 明确记录该平台的回归终态。
关闭 `run_journal` 时，Mac 直接从已部署并审计的 Universal App 生成 DMG，不启动回归任务，
也不创建回归结果附件；安装包附件中的 `mac-stages.json` 和 `build-result.json` 明确记录
`regression.status=skipped`、`reason=run_journal=false`。

附件名称含平台、run ID、attempt，保留 7 天。安装包附件包含 EXE/DMG、对应 SHA-256 文件和
`build-result.json`；Mac 另含 `mac-stages.json` 及 DMG 日志，并单独上传回归目录，包括固定输入、
执行日志、逐场景自动结果和截图证据。回归失败时仍上传已生成的安装包及失败证据。
使用标准 upload-artifact；不补上传，不自动续传失败的发布。Mac 回归通过只证明所选场景
覆盖的行为，不代表已完成安装器安装、所有 UI 或全部算法验收，当前 Mac 签名不等同已公证发行。

生产和消费源码子模块统一使用 `core.autocrlf=false` / `core.eol=lf`，仓库 `.gitattributes` 优先；不改写来源清单。旧 SDK 若记录不同检出字节，需要按标准 SDK 更新入口重建匹配版本，不能绕过校验。

## 自动发布到 Releases

`publish_release` 默认 `true`。Windows、macOS 及启用的 Journal 回归、附件上传和队列看护汇总
全部成功后，独立发布任务只下载本轮 run ID / attempt 的两个安装包附件。关闭此开关会跳过
发布任务，不要求 RELEASE Secret，不改变安装包构建、检查与 Actions 附件上传。

发布前核对两平台的 App/SDK SHA、架构、SDK 消费检查、各自启用的回归终态及安装包 SHA-256。
标签为 `build-<run_id>-<attempt>`，明确指向本次固定的 App SHA，不默认使用 `main` 或 workflow
控制提交。先创建预发布草稿，上传 EXE、DMG、各自的 SHA-256 文件和
`build-result-windows.json` / `build-result-macos.json`。同名构建信息分别命名，避免相互覆盖；
Mac 日志与逐场景回归证据继续保留在 Actions 附件中。
远端附件名称、大小、摘要全部核对成功后才公开为 Pre-release；不会自动设置为正式版 Latest。
发布或校验失败会使 workflow 失败，已创建的草稿保留供检查，不公开不完整的下载集合。
全量重跑使用新的 attempt 标签，不覆盖之前发布或已验收的安装包。

这个开关只控制是否发布，不控制预发布/正式版。自动发布的每一条都先是预发布版；
验收通过后，在 GitHub Releases 编辑该条记录，取消 Pre-release，可按需要标记为 Latest。
此过程沿用同一标签和同一批已验收附件，无需重编；其他构建记录仍保持预发布状态。
关闭 Journal 时，Release 说明和 Mac 构建信息明确标记 `skipped`，不会声称回归通过。

创建非默认分支提交的标签时，GitHub 可能要求发布凭据具有 Workflows 写权限；
请为 `GPLATFORM_RELEASE_TOKEN` 配置仅限本 App 仓库的发布身份，不复用只读 SOURCE。
权限依据见 [创建 Release](https://docs.github.com/en/rest/releases/releases#create-a-release)。

## 失败、重跑和清理

选择 **Re-run all jobs** 或重新发起 workflow。**Re-run failed jobs / 单个 job 重跑不受支持**：prepare 的 attempt 与本轮不一致时，平台任务和 guard 在调度前跳过，汇总明确失败并提示全量重跑，避免沿用旧时限或遗漏 guard。

普通失败和取消执行独立收尾，上传后删除本任务的安装包、回归结果暂存和临时 control checkout。固定 source 工作缓存保留 Git 对象与当前平台 Release preset 构建目录；构建前验证固定 SHA、目录边界与已有 CMake 缓存，再清除源码和子模块的改动及其余未跟踪文件。子模块按每层固定父提交同步 URL；每次仍执行原有 CMake 配置、构建、检查和打包。源码目录、配置、架构或显式 Qt/工具链输入与缓存冲突时明确失败，不能绕过缓存检查。外部兼容依赖缓存继续保留。清理校验工作区边界与 Git 标记，仅针对 Windows 只读权限错误解除写保护重试，不忽略其他删除错误。不删除无关工作区，也不清全机缓存。硬终止或 runner 故障不能保证收尾执行，需如实记录剩余状态。

## 本地验证

```sh
python3 -m unittest discover -s tests/ci -p 'test_*.py' -v
actionlint .github/workflows/build-app.yml
```

真实双平台验收仍要通过本 workflow 运行；本地 fixture 与语法检查不代替 Windows/Mac 实际结果。

## Windows 标准准备

Windows 资产位于 TestAssets 的 `windows` 分支。App 沿用开发分支，默认资产 gitlink 不改为平台专属分支。
业务场景采用当前 App 集成版本，保留 macmini 的输入修正；Windows 标准单独采集、审核。
包含部署资源绝对路径的完整 DB 标准必须在 `preconditions.applicationDirectory` 声明实际应用目录。
部署目录不同，场景可以用 `deploymentResources` 显式声明资源相对路径及审核过的 SHA256。
现有 Journal 物化阶段先核对当前部署文件哈希，再仅替换标准 DB 中对应的完整 UTF-8 路径字符串及长度。
原始标准与实际 DB 不改写，正式比较仍覆盖全部字节；资源变化、缺失或标准内路径不明确均拒绝运行。
没有该声明的旧场景仍要求原部署目录。此绑定不证明独立 App 已通过，仍须执行 CI 正式回放。

候选采集使用同一个入口增加 `--capture-candidates`：在相同 entryIndex 将 verify.db/verify.screenshot
物化为已有 capture.db/screenshot.capture，保留原始 Journal、业务输入、时间点与标准。
运行目录记录原始和执行 JSON 的哈希、实际窗口、DB 原始字节及截图。
候选完成返回 not_verified（退出码 2），不会写入资产或通过 CI。逐检查点审核后才更新 Windows 标准，
随后在新目录运行不带该选项的完整回归。CI 拒绝待审核标准、候选采集和不完整的场景清单。

Windows 开关独立于现有 macOS 开关；关闭任意平台的回归仅记录该平台 skipped，发布阶段分别检查两平台终态。
