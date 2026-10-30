# App 双平台构建

本仓库只有一个 `Build App` workflow，同时构建 Windows x64 和 macOS Universal。入口为 Actions → Build App → Run workflow；主仓库的 Run Build App 也调用同一入口并等待真实结果。

仅手动或被主仓库显式调用，不监听源码 push，不更新 SDK，不创建 Release。

## 输入和版本

| 输入 | 默认值 | 含义 |
| --- | --- | --- |
| `source_ref` | `feature/gplatform-ui` | App 已有分支或完整提交 SHA |
| `request_id` | 空 | 主仓库内部关联 ID；独立手动运行可留空 |

准备任务固定 App SHA，读取其中 `ThirdParty/libs/GPlatformSDK` 的 gitlink。两平台消费同一个 App SHA 和 SDK SHA，不提供“最新 SDK”或单独切换源码依赖的选项。

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
| `GPLATFORM_SOURCE_TOKEN` | App、SDK、私有源码子模块的 Contents 读 |
| `GPLATFORM_WORKFLOW_TOKEN` | 供 hosted prepare 查询本仓库 runner 的 Administration 读 |

平台编译 job 仅注入 SOURCE，不注入独立发布 Secret；SOURCE 应配置为只读。实际权限由配置的 Token 决定，Secret 命名不会降低权限；经明确授权采用更广权限时，验收记录必须注明，不能宣称仍满足只读隔离。queue_guard 用本仓库 `GITHUB_TOKEN` 的 Actions 写权限，只取消本次运行。凭据配置由获授权的管理身份完成。

runner 需要 Git/LFS、Python 3.12、CMake/Ninja 和相容 Qt。Windows 需要 PowerShell 7、MSVC C++/Windows SDK、Inno Setup 7；Mac 需要激活的 Xcode 和可运行 x86_64 检查的 Rosetta。固定的 Node 24 Actions 要求 runner 至少 2.327.1。

环境部署在 workflow 之外完成：安装 Python >= 3.12，并使 runner 服务的 PATH 能直接执行 python；Windows 的 PowerShell 脚本执行策略也由部署阶段配置。Mac 可将已安装 Homebrew Python 的 libexec/bin 加入 runner PATH。修改 runner 环境后，在空闲时重启对应服务并检查工具。workflow 不下载或安装工具，不改 PATH、不创建解释器别名、不设置执行策略；只读检查 Python，环境缺失直接失败。

工具路径沿用普通项目入口支持的 `Qt6_DIR` / `GPLATFORM_VS_ROOT` 等环境配置，不提交机器绝对路径。`GPLATFORM_MACHINE_BUILD_LOCK` 可指定跨仓库共享锁路径；默认用户缓存目录 `.cache/gplatform/build.lock`。`CMAKE_BUILD_PARALLEL_LEVEL` 控制编译并行度。

## 执行和附件

Windows 复用 `scripts/build.ps1`，Mac 复用薄 preset 和 `scripts/build_macos_universal.py --merge-only --dmg`，保留项目默认构建目录。

每平台必须实际构建并运行 `SDKConsumerExtensionCheck`，再执行安装包入口；Mac 两架构分别检查。两平台独立完成，任何检查、编译、打包或附件上传失败均导致整体失败。

附件每平台一份，名称含平台、run ID、attempt，保留 7 天。内容只有安装包（EXE/DMG）、对应 SHA-256 文件和 `build-result.json`。使用标准 upload-artifact；不补上传、不提供 Release/resume 框架。结果不代表已完成安装启动、完整 UI 或算法回归，当前 Mac 签名不等同已公证发行。

生产和消费源码子模块统一使用 `core.autocrlf=false` / `core.eol=lf`，仓库 `.gitattributes` 优先；不改写来源清单。旧 SDK 若记录不同检出字节，需要按标准 SDK 更新入口重建匹配版本，不能绕过校验。

## 失败、重跑和清理

选择 **Re-run all jobs** 或重新发起 workflow。**Re-run failed jobs / 单个 job 重跑不受支持**：prepare 的 attempt 与本轮不一致时，平台任务和 guard 在调度前跳过，汇总明确失败并提示全量重跑，避免沿用旧时限或遗漏 guard。

普通失败和取消执行独立收尾，删除本任务的安装包暂存和临时 control checkout。固定 source 工作缓存保留 Git 对象与当前平台 Release preset 构建目录；构建前验证固定 SHA、目录边界与已有 CMake 缓存，再清除源码和子模块的改动及其余未跟踪文件。子模块按每层固定父提交同步 URL；每次仍执行原有 CMake 配置、构建、检查和打包。源码目录、配置、架构或显式 Qt/工具链输入与缓存冲突时明确失败，不能绕过缓存检查。外部兼容依赖缓存继续保留。清理校验工作区边界与 Git 标记，仅针对 Windows 只读权限错误解除写保护重试，不忽略其他删除错误。不删除无关工作区，也不清全机缓存。硬终止或 runner 故障不能保证收尾执行，需如实记录剩余状态。

## 本地验证

```sh
python3 -m unittest discover -s tests/ci -p 'test_*.py' -v
actionlint .github/workflows/build-app.yml
```

真实双平台验收仍要通过本 workflow 运行；本地 fixture 与语法检查不代替 Windows/Mac 实际结果。
