# Journal 协议、执行与证据

下面源码路径相对完整宿主 `HOST_ROOT`。独立 App 的已有场景在 `APP_ROOT/tests/journals`，但不能假定完整 Python 工具也已随 App 交付。

## 真实定义与场景

- `ThirdParty/JournalSystem/tools/journal_runner.py`：`load_journal()`、允许的 entry kinds、回放、比较与 `finish_result()`。
- `ThirdParty/JournalSystem/tools/journal_execution_session.py`：CLI/GUI 共用的启动、握手、环境、结果目录和清理。
- `ThirdParty/JournalSystem/tools/journalsystem_gui.py`：GUI 使用同一个 session。
- `Core/tests/journals/README.md`：应用场景约定、受控模型与独立参考。
- `Core/tests/journals/machine_selector`：标准鼠标/键盘输入及 screenshot.capture 示例。

## 先复用场景、资产和执行能力

从现有场景核对真实输入入口、前置配置、对象生命周期、可观察终态与独立预期。复用操作片段时重新确认 objectName、坐标与时序适用于当前界面，不将旧场景中的 opaque DB ID 或当前输出当成新真值。
宿主 `Core/tests/journals/assets/basic/manifest.json` 与 `operations.json` 包含受控模型、几何/路径参考及用途；`scripts/generate_basic_test_data.py` 是已有生成/检查入口。先检查是否覆盖当前需求，再补确实缺少的最小资产，不为每个操作复制一个模型目录。共享资产变更需检查依赖它的场景和预期。
`journal_runner.py` 已有 `load_journal()`、比较器、`finish_result()`；`JournalExecutionSession` 负责进程/握手/清理，`session_repository.py` 负责会话目录和文件管理。只增加场景时直接使用这些能力，不再实现手工发送/调度、截图比较或报告汇总。
测试资产需要来源、单位和独立预期即可，不添加与现有协议平行的业务查询/断言系统。扩展正式观测能力前先判断现有原生检查或断言是否足够；缺少能力时明确缺口，不能用注入状态或绕过生产入口补出“通过”。

## 文件结构

```text
<scenario>/
├── events/Journal.json
├── metadata.json
└── <必要输入资源>
```

Journal 顶层仅 `formatVersion: 2` 与 `entries`；每条仅 `atMs/kind/payload`，时间非负且递增或相同，相同时按数组顺序。不要写 `sequence` 或把元数据重复进 Journal。
当前 kind 包括 `input.mouse/key/pointer/wheel/dragDrop/close`、`action`、`capture.db`、`verify.db`、`verify.screenshot`；添加内容前以 `ENTRY_KINDS` 和实际 payload 校验为准。
`metadata.json` 保存名称/描述，必要时含 `applicationEnvironment`；它是独立进程的设置，不是 Journal entry。

## 输入与等待

先查真实 QML `objectName` 与窗口，如 `window.objectName="gplatform.main"`。目标定位下 `position` 是控件局部逻辑坐标；固定窗口场景的绝对逻辑坐标按其布局前提使用，不拿高 DPI 截图的物理像素直接点击。
鼠标 press/release 与 keys/buttons 状态配对，确认滚动、焦点和弹层。当前执行器按声明调度标准 Qt 输入，不负责替你调整窗口或另找点击点。
不能创造 `wait`、`assert` 或 execute-QML 等未知 kind；先查现有工具如何处理固定时间与异步完成。业务仍 pending 时不提前读取终态。
`file.import` 的相对输入路径由 runner 以 Journal 所在目录解析，截图 action 的相对路径有结果输出语义；依当前实现确认，不从调用 shell 的 cwd 猜路径。

## 执行会话

`journal_runner.py --run ... --app ... --output ...` 使用共享 JournalExecutionSession。指定 `--app` 时启动独立实例、选择本地端口，以 replay ID 验证身份和 ready；不要向碰巧监听的另一个程序发送命令。
`--output` 必须是新目录。保留输入哈希、entry 结果、startup log、`execution_result.json` 与 `verification_report.html`。结果路径通过正式服务与 session 获取，不硬编码开发者日志目录。
包含 `applicationEnvironment` 的场景需要由 session 启动程序；测试 fixture 环境需与实际设备任务区分。关闭/清理失败也纳入执行结果，不能丢弃诊断后重写为成功。
`HOST_ROOT/scripts/run_journals.py` 当前运行批量验收的路径有 Windows 设置恢复逻辑，非 Windows 只可列举；macOS/Linux 使用共享正式 runner/session，不能照搬批量入口。

## 断言与结果

`verify.db` 比较声明格式的独立 expectedSerialized，当前格式为 `transdb.cereal-portable-binary.v1`；`capture.db` 只收集数据。截图验证需要有效的独立 baseline 与合法 tolerance；截图 action 本身不是验证。
`finish_result()` 在非空执行、无执行/清理错误且没有 failed/not_run 的基础上计算 success；存在通过的 `verify.*` 才给 `acceptance=passed`，无 verify 为 `not_verified`。
正式 CLI 返回码：passed 为 0，执行失败为 1，执行成功但未验证为 2。保留 acceptance 与 success 的区别。
用户要求几何/文件/业务状态验收时，结果需要相应证据。独立检查可以与 Journal 配合，但应单独说明结论范围，不能修改正式报告伪装已有内置断言。
现有历史场景可能只有操作与截图，先评估覆盖，不因在场景目录中就视作已通过用例。
