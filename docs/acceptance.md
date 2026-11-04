# 应用验收架构

## 仓库与所有权

测试资产唯一来源为 [GeoPlatform-TestAssets](https://github.com/dezbracaty/GeoPlatform-TestAssets)。
App 在 `tests/TestAssets` 以子模块固定提交；完整宿主使用 `App/tests/TestAssets`，不再保存另一份模型或场景。
普通构建按需初始化测试资产；完整宿主启用 `GPLATFORM_BUILD_TESTS` 时需要该固定版本。
资产仓库只拥有模型、Journal、metadata、预期、基线、格式和确定性生成器。

App `tools/acceptance.py` 拥有业务验收编排与最终结论；`tools/acceptance_checks.py` 拥有产物检查。
正式 JournalSystem 拥有进程启动/握手、Qt 输入回放、原始 DB/截图检查、清理和执行报告。
主仓库 `scripts/run_journals.py` 只是共享入口的兼容转发，不再在内存中替换执行器源码。
完整宿主私有 C++ 回归继续留在 `Core/tests`，只改为读取固定资产。

场景验收只提供 DB 对比和标准截图对比两种方式，具体约定见
[验证方式与复用契约](verification-methods.md)。注册表由 App `tools/verification_methods.py` 唯一维护。
CTest、Python、Journal 是执行入口；资产预检与操作回放成功不单独构成业务正确性证明。

## 输入与场景契约

`catalog.json` 引用模型清单；模型清单记录稳定 ID、来源、单位、字节数、SHA-256 和独立预期。
迁移保留原始模型字节；大型导入 STL 使用 LFS，检查时必须是真实文件。
每个场景固定为 `scenarios/<id>/events/Journal.json` 和同级 `metadata.json`。
Journal 仍使用现有 v2 协议，不引入新 RPC、输入调度或业务查询 kind。

metadata v3 声明 `id/name/testPurpose/scope/inputs/unverifiedScopes/verification.requiredMethods/acceptanceTarget`；
有自动产物检查的场景还声明 `artifacts/checks/expected`，格式见资产仓库 `schemas/scenario.schema.json`。
模型通过 `namespace/id` 引用。Journal 实际导入路径必须与声明一致，且不能逃逸资产目录。
生产 Action 创建的模型记录 Action 和完整参数，并与 Journal 核对。
模型几何预期来自解析构造；任务预期来自明确业务要求，不能直接采纳当前错误输出。

## 统一入口与处理顺序

`tools/run_acceptance.py` 同时支持完整宿主和独立 App。独立 App 缺少正式执行器时，
通过 `--journal-tools <JournalSystem/tools>` 提供；缺失即明确失败，不另建私有 RPC 执行器。

1. 校验场景格式、模型实体/哈希、输入对应关系、检查器和输出声明。
2. 记录资产、App、Journal、metadata、模型、执行/验证代码与程序/已部署库的来源。
3. 调用正式 `JournalExecutionSession` 启动独立实例并完成回放与清理。
4. 核对全部步骤和输入哈希；检查声明产物是本次运行生成，归档并核对复制前后哈希。
5. 按固定注册表调用公共检查器，并汇总每类必需方法的结果；metadata 不能提供任意脚本或 shell 命令。
6. 输出每项检查、场景最终结论与批次汇总。每次输出目录必须是新目录。

公共检查器 `gcode-job` 参数化检查层数、立方体尺寸、喷头高度段、耗材、多喷头配置、裙边/擦料塔、
打印区、温度等待、挤出截面积和包内外 G-code 一致性，作为既有专项检查单独记录，不满足 DB 或截图验收。
DB 与截图验收分别复用正式 `verify.db` 与 `verify.screenshot`，入口自动纳入方法结果。
该检查器验证软件刀路，不验证固件宏和实机打印。

## 结果目录与判定

```text
<run>/summary.json
<run>/<case>/execution_result.json       Journal 原始结果
<run>/<case>/verification_report.html    Journal 原始报告
<run>/<case>/application.log
<run>/<case>/screenshots/
<run>/<case>/artifacts/                  本次实际产物
<run>/<case>/reports/                    业务检查证据
<run>/<case>/checks.json
<run>/<case>/case_result.json            场景业务结论
<run>/<case>/case_report.html
```

内部结果引用使用相对路径。`case_result.json` 是业务结论唯一来源；HTML 与汇总引用该结论。
预检阻塞时没有虚构的 Journal 执行文件；执行路径为空并保存具体原因。
结果记录源码提交和 dirty 状态，同时保存关键文件哈希；未提交验证也可以追溯到实际输入。
v3 结果用 `verificationPlan` 和 `methodResults` 记录所选的 DB/截图验收，专项文件结果另列 `supplementalResults`。

| 状态 | 条件 |
| --- | --- |
| `passed` | 执行及清理成功、步骤完整、全部必需方法均有对应必需检查且全部通过 |
| `failed` | 执行/清理失败、运行后缺产物、使用旧产物、收集时变化或必需检查不符 |
| `blocked` | 启动前缺资产/实体、输入损坏、场景声明错误、未知检查器或缺程序/输出目录配置 |
| `not_verified` | 已执行但缺少任一必需方法的检查，或必需检查未完成 |

Journal `acceptance` 原样保留。只有对应的正式 DB 或截图检查完成后，场景才能通过；专项文件检查不能补出通过。
无断言的老场景迁移后仍为 `not_verified`，不能因 JSON 存在、截图保存或程序退出正常判通过。
批次任一失败则失败，其次为阻塞、未验证，全部通过才通过。退出码依次为 1、1、2、0。

## 使用与扩展

```powershell
git -C App submodule update --init --checkout tests/TestAssets
git -C App/tests/TestAssets lfs pull
python App/tools/run_acceptance.py --list
python App/tools/run_acceptance.py --list-methods
python App/tools/run_acceptance.py --application build-Release/GPlatform/GPlatform.exe --cases p3_height_color_export --exports "$env:USERPROFILE/Downloads/GPlatform/Gcode" --output build-Release/Testing/AppAcceptance/new-run
```

GUI 默认场景目录也指向资产仓库。GUI 直接回放显示的仍是 Journal 原始验收，
完整业务验收使用上述共享入口，不能用 GUI 的执行成功替代最终 `case_result.json`。

增加场景先确定数据验收还是画面验收，再补对应标准 DB 或标准截图，复用现有比较代码。
截图基线需要独立审批与平台/DPR/主题/容差来源。实机结果不属于本轮场景验收。
