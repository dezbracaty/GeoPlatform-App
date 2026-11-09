---
name: gplatform-journal
description: 编写或执行 GPlatform 的真实应用 Journal 验收。先明确可观察结果，复用已有场景、输入资产和正式执行器，按现有协议补必要断言并报告真实覆盖。
---

# Journal 应用验收

本技能随 App 保存，正式 Python 工具唯一实现位于 `APP_ROOT/tools/journal/`，随 App 一起交付；相关测试位于 `APP_ROOT/tests/journal/`。模型与场景由 `APP_ROOT/tests/TestAssets` 固定子模块维护，入口为 `scenarios/<id>/events/Journal.json`；通过 `tools/run_acceptance.py` 统一执行及业务验收，规则见 `docs/acceptance.md`。先确认实际工具、场景、被测可执行文件和验证资产；独立 App 默认使用随仓库提供的工具，`--journal-tools` 仅用于显式开发覆盖，不能改用自造私有 RPC 冒充同一验收。

## 明确验收入口

先确定用户要求验证的是输入控件、正式 Action、业务状态、输出文件还是视觉效果。菜单/键盘/拖拽需求必须经过相应真实输入；直接 Action 可以验证业务入口，但不能声称覆盖了文件选择或 UI 输入。

## 先查已有场景和工具

查最接近场景的 Journal、metadata、输入资产、预期来源和当前页面/Action；不能只复制旧坐标、DB ID、延迟或截图路径。确定本次需要触发什么、观察什么，以及哪些现有断言已能覆盖。
优先使用可复用的受控模型、独立参考和现有 JSON/runner/session/repository。增加普通场景只写声明式场景与必要资产，不为每个用例创建专属 Python 执行器、私有 RPC 或另一套报告。
场景 metadata v3 只选择 DB 验收（db）或标准截图验收（screenshot），并用 acceptanceTarget 写清目标，遵循 docs/verification-methods.md。数据场景的截图默认仅诊断；先补标准 DB/标准截图并核对，再用已有 verify.db/verify.screenshot 重放。每个场景不单独配置验证脚本，不扩展宽泛的规则体系。
已有协议能表达时直接使用；确需新观测能力时先检查现有业务/测试入口，再区分应补原生检查还是扩展正式协议。新 kind/字段必须有明确需求、实现和验证，不在场景里先编一个工具不支持的入口。

## 构造和执行

1. 优先复用或调整已有最小场景；新行为才增加相应场景。JSON、资源与 sidecar 按 [协议与证据](references/protocol-and-evidence.md) 组织，保留仍有独立覆盖价值的旧用例。
2. 从真实 `objectName`、窗口与当前布局确认输入定位，发送标准 Qt 输入事件；不调用 QML 方法或注入业务状态。
3. 定义独立预期：DB 序列化、允许的截图基线或任务所需的几何/输出检查。当前错误输出不能未经确认成为通过基线。
4. 使用 `journal_runner.py` 或 GUI 共同使用的 `JournalExecutionSession`，新建结果目录，保留输入哈希、启动日志、执行结果与报告。
5. 检查执行、业务异步终态、验收断言和清理结果；需要独立检查的产物另外核对并标明范围。

## 完整业务流程验收

用户要求“每项都测”或“按流程全部跑一遍”时，先按当前菜单、模式和业务分支列出覆盖矩阵，再逐项运行对应 JSON。覆盖所需的参数输入、生成、预览、文件选择器导出、结果采纳及后续操作；源码审核、原生 CTest 和离屏控件测试分别提供证据，不能代替真实应用流程。校准任务另读 [校准流程与证据](references/calibration-workflows.md)，复用已入库场景。

每项结果应能追溯到实际执行的 JSON 路径/哈希、运行目录、正式 acceptance 和对应业务产物。按任务独立核对记录或文件，保留正式报告原值；没有内置断言时仍报告 `not_verified`，并明确哪些检查另行完成。只保存 JSON、截图或测试清单不等于已经执行和验收。

出现失败时，在已授权范围内继续定位环境、输入、预期或生产实现，必要时修正并定点复跑；保留失败证据和复跑结果。可继续调查的问题不能仅记为“已知失败”就结束验收；真正无法继续时说明具体阻碍和未完成项，不扩大通过结论。

## 正式 CLI 示例

在 App 根目录，先将占位路径换成核实后的实际绝对或相对路径：

```sh
python3 tools/journal/journal_runner.py \
  --run <scenario>/events/Journal.json \
  --app <executable> \
  --output <new-result-directory>
```

`--app` 接受可执行文件，不接受 `.app` 目录。已有应用可使用 `--host/--port`；包含 `applicationEnvironment` 的场景要求由 session 启动应用。

## 解释结果

- `acceptance=passed`：执行成功且有通过的 `verify.*`。
- `acceptance=not_verified`：执行完成但没有验收断言；不是自动验收通过。
- `acceptance=failed`：执行、断言或清理等存在失败，保留错误。

截图动作成功只证明文件生成；原生业务日志、DB、文件或独立几何检查各自支持相应结论。报告实际覆盖与剩余未验证范围，不用测试清单存在或 CLI 成功代替验收。
完成前核对场景是否经过所需真实入口、断言是否独立有效，以及是否为测试额外改变了产品行为、共享资产或无关执行器。失败时先定位协议、环境、预期或产品问题；涉及修复则应用 `gplatform-bugfix`，不靠放宽断言获得通过。
