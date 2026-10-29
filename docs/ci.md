# App 仓库构建

GitHub → GeoPlatform-App → Actions → **App Windows** / **App macOS Universal** → **Run workflow**。
工作流在默认分支 `feature/gplatform-ui` 提供手动入口。产品逻辑仍使用 App 自己的 build.sh / build.ps1、CMake installer 和 Universal 脚本。
本流程消费已发布的二进制 SDK，不重新生成 SDK，也不修改 SDK 仓库。

## 自动运行与分支菜单

包含这些工作流的分支，每次 push 都会分别构建 Windows 安装包和 macOS Universal DMG，固定使用触发 push 的完整 SHA。
一次 push 包含多个提交时构建最终 HEAD。新分支从默认分支创建即可继承自动构建；没有工作流的旧分支可从默认入口手动选择，也可合入本次 CI 文件后自动运行。
SDK 及源码子模块默认使用触发提交记录的 gitlink，不跟随菜单中的分支变化。

防循环由 Git 内容判断实现，提交说明、提交人均不参与判断：

- 仅两份 workflow 内严格校验的分支选项和对应默认值改变：通过轻量检查，跳过构建及后续菜单刷新。
- 源码、SDK/source gitlink、脚本、工作流执行逻辑或混合改动：照常构建。
- 新分支或无法可靠获得旧提交：保守地构建。

构建结束后查询 App、SDK、OrcaSlicer、SmoothUI、SmoothUIDock 的真实远端分支，在一个提交中刷新默认分支的两份菜单。
两平台共享刷新并发组，遇到并发源码提交时重新读取最新默认分支再生成，禁止强推。
构建失败也尝试刷新；取消的运行不刷新。没有定时或分支事件驱动的独立菜单任务。
菜单是上一次成功刷新的快照；新建的依赖分支在下一次构建结束后出现。已存在的选项会在运行时重新解析分支头。
菜单无需独立仓库或长期 CI 分支，也无需记住任何提交说明标记。

## 手动选项

| 选项 | 行为 |
| --- | --- |
| source_ref | 实际 App 分支；默认仓库的 default_branch |
| sdk_ref | Use repository pin 或 SDK 仓库实际分支；选中的 SDK 必须匹配 App 的平台、Qt、编译器与源码依赖 |
| orca_ref / smoothui_ref / dock_ref | Use repository pin 或对应源码子模块的实际分支；显式选择优先 |
| task | installer 默认编译、检查并打包；build 仅编译和检查，不上传附件 |
| dependencies | locked 使用记录的源码 gitlink；latest 更新三个源码 fork 的配置分支，SDK 仍按 sdk_ref 处理 |
| delivery | artifacts 默认提供 Actions 下载；release 创建 App 仓库不可覆盖的正式 Release |
| version | Release 必填，必须等于 App 项目现有版本，不自动修改版本 |
| clean_build | 清理本配置编译目录，保留兼容的依赖缓存 |
| resume_run | 恢复本机同平台既有成功安装包运行，如 123456789-1，校验 SHA-256 后重新上传/发布，不重建 |

自动 push 固定 installer / locked / artifacts，不创建 Release。
Mac 下载只有 DMG 和 checksums.sha256；Windows 下载只有 EXE 和 checksums.sha256，保留 30 天。
本机 runs 保存日志、来源清单和可恢复的 source.bundle。源码通过 Git 同步，不作为下载附件上传。
Release 同样仅上传安装包和校验文件；Mac tag 为 v<version>-macos-universal，Windows 为 v<version>，不覆盖既有发布。

## Runner 与权限

App 使用本仓库独立注册的 runner；GPlatform 的 repository runner 不能直接领取 App 作业。
Mac 标签 self-hosted/macOS/ARM64/gplatform-macos-build，Windows 标签 self-hosted/Windows/X64/gplatform-build。
默认目录分别为 ~/gp-app-ci 和 C:/gp-app-ci，均有独立的 runner、source、build、runs 和 config/machine.json。
仓库变量 GPLATFORM_APP_MACOS_CI_ROOT / GPLATFORM_APP_WINDOWS_CI_ROOT 可指定绝对根目录。
机器配置参考生产端配置，修改 root；fetch_cache 复用同机兼容依赖缓存，machine_lock 必须指向生产端 gp-ci/pipeline.lock，以串行化同一机器上的两仓库构建。
Mac launchd 服务需要对应账号登录；Windows 在有管理员权限时安装服务，否则使用当前账号的有限权限登录计划任务，并要求该账号保持登录。
Windows runner 需要读取 VS、Qt、Inno Setup 和缓存的权限。

| Secret | 用途 |
| --- | --- |
| GPLATFORM_SOURCE_TOKEN | 读取私有源码/SDK 子模块；公开仓库也可使用 GITHUB_TOKEN 读取 |
| GPLATFORM_WORKFLOW_TOKEN | 在默认分支提交 workflow 菜单，需对应仓库 Contents 和 Workflows 写权限 |

本仓库 GITHUB_TOKEN 仅在 build 作业授予 Contents write，用于手动请求的 App Release；不向 SDK 或源码 fork 推送。
只监听本仓库分支 push / 手动运行，不监听外部 PR。构建配置不提交机器路径或凭据。

## 本地检查

```sh
python3 -m unittest discover -s tests/ci -p 'test_*.py' -v
npm ci --prefix scripts/ci/artifact-upload --ignore-scripts --no-audit --no-fund
node --test scripts/ci/artifact-upload/test-retry.cjs
```

Windows 初始化依赖时优先复用同一台机器上已有、远端地址一致的 Git 仓库；仍按本次 gitlink 获取缺少的提交，并执行 SDK 版本和内容校验。复用仅包含 Git 对象，不包含旧工作区未提交内容，不依赖旧运行目录的保留。

Windows CI status 是手动只读入口：输入主仓库 SDK 构建的 run-id-attempt，可用同机 App runner 查看 producer 的日志尾部和编译进程，不获取构建锁、不修改源码或发布指针。自定义 producer 目录时设置 App 仓库变量 `GPLATFORM_WINDOWS_PRODUCER_ROOT`（默认 `C:/gp-ci`）。

Windows 与 SDK producer 统一使用 `core.autocrlf=true` 的 Git 检出规则，仓库 `.gitattributes` 优先。标准 CMake 和 CI 共用来源校验：Git 检出直接核对实际 fork 内容、提交及工作区状态与所选平台 SDK 一致，不改写源码目录中的来源清单；纯文件分发包仍严格验证随包清单。
