---
name: gplatform-journal
description: 编写或执行 GPlatform 的真实应用 Journal 验收。先明确可观察结果，复用已有场景、输入资产和正式执行器，按现有协议补必要断言并报告真实覆盖。
---

# Journal 应用验收

本技能随 App 保存，正式 Python 工具当前位于完整宿主的 `ThirdParty/JournalSystem/tools/`。先确认实际工具、场景、被测可执行文件和验证资产；独立 App 缺少工具时说明依赖，不能改用自造私有 RPC 冒充同一验收。

## 明确验收入口

先确定用户要求验证的是输入控件、正式 Action、业务状态、输出文件还是视觉效果。菜单/键盘/拖拽需求必须经过相应真实输入；直接 Action 可以验证业务入口，但不能声称覆盖了文件选择或 UI 输入。

## 先查已有场景和工具

查最接近场景的 Journal、metadata、输入资产、预期来源和当前页面/Action；不能只复制旧坐标、DB ID、延迟或截图路径。确定本次需要触发什么、观察什么，以及哪些现有断言已能覆盖。
优先使用可复用的受控模型、独立参考和现有 JSON/runner/session/repository。增加普通场景只写声明式场景与必要资产，不为每个用例创建专属 Python 执行器、私有 RPC 或另一套报告。
已有协议能表达时直接使用；确需新观测能力时先检查现有业务/测试入口，再区分应补原生检查还是扩展正式协议。新 kind/字段必须有明确需求、实现和验证，不在场景里先编一个工具不支持的入口。

## 构造和执行

1. 优先复用或调整已有最小场景；新行为才增加相应场景。JSON、资源与 sidecar 按 [协议与证据](references/protocol-and-evidence.md) 组织，保留仍有独立覆盖价值的旧用例。
2. 从真实 `objectName`、窗口与当前布局确认输入定位，发送标准 Qt 输入事件；不调用 QML 方法或注入业务状态。
3. 定义独立预期：DB 序列化、允许的截图基线或任务所需的几何/输出检查。当前错误输出不能未经确认成为通过基线。
4. 使用 `journal_runner.py` 或 GUI 共同使用的 `JournalExecutionSession`，新建结果目录，保留输入哈希、启动日志、执行结果与报告。
5. 检查执行、业务异步终态、验收断言和清理结果；需要独立检查的产物另外核对并标明范围。

## 正式 CLI 示例

在完整宿主根目录，先将占位路径换成核实后的实际绝对或相对路径：

```sh
python3 ThirdParty/JournalSystem/tools/journal_runner.py \
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
