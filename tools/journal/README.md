# Journal 场景测试与录制

本目录是随 App 维护的唯一 Python 工具实现，相关测试在 `tests/journal/`。
下述命令从 App 仓库根目录执行，完整宿主可以先进入 `App/`。
SDK 提供应用端 C++ Journal 服务。场景和模型通过 `python3 scripts/prepare_test_assets.py --ref <branch-or-full-sha>` 从 App 根目录准备到 `tests/TestAssets`。工作台默认使用该目录，也可通过 `--assets` 指定已准备的检出。

Journal 的交互入口是网页工作台。电脑 A 运行 Python 服务和 GPlatform，电脑 B 使用浏览器访问；录制、回放、比较、输入与结果文件均保留在 A。浏览器刷新或断开不会停止任务。

## 启动工作台

```bash
python3 -m pip install -r tools/requirements.txt
python3 tools/journal/journal_server.py --application /path/to/GPlatform
```

启动后显示本机地址与随机6位数字连接码，并打开浏览器。Python 无需 Tk。局域网使用：

```bash
python3 tools/journal/journal_server.py --application /path/to/GPlatform --listen 0.0.0.0
```

B 打开 `http://A的局域网IP:8765` 并输入 A 终端显示的连接码。服务端口默认8765，应用 Journal 端口默认9999，两者独立。所有页面使用同一个 A 端工作台。只在可信局域网使用 HTTP；跨不可信网络时通过 SSH 隧道或受保护的 HTTPS 入口访问，不直接公开端口。连接码不写入 URL 或网页本地存储。登录后使用独立的长随机会话凭证，刷新网页保持登录；同一IP连续输错5次暂停登录60秒，已登录会话不受影响。短连接码只用于登录，不作为Bearer凭证。默认每次启动重新生成，也可通过 `GPLATFORM_WORKBENCH_TOKEN` 指定6位数字连接码（保留前导零）。

工作台提供“场景库”“回放记录”“录制”。设置中的程序与导出目录均为 A 上的路径。场景默认来源为服务启动指定的 App `tests/TestAssets`；需要使用独立平台资产时，通过 `--assets <目录>` 显式指定，实际目录显示在服务状态并写入正式报告的 `assetsRoot`。网页不编辑标准。勾选决定本次批次，搜索只筛选显示，启动后队列固定。

导出目录默认无需配置：工作台使用执行电脑 A 的 Python `tempfile` 系统临时目录（macOS 通常为 `$TMPDIR`，Windows 为 `%TEMP%`，Linux 通常为 `/tmp`），为每次需要导出的回放创建独立的 `gplatform-journal-exports-*` 目录，具体路径记录在本次任务日志中。批次内的同名文件继续由正式执行器按场景隔离。可用 `--exports` 或网页设置显式指定 A 上已有的目录；清空设置即恢复默认临时目录。正式报告照常归档声明产物；原始临时导出文件保留供诊断，也可能被系统临时文件清理机制清除。

服务调用唯一正式入口 `tools/run_acceptance.py`，复用独立实例启动、业务验收、后台整图比较和进程清理。场景共用既有数据；执行器仅准备声明的主题，不备份、恢复或删除 App INI，不覆盖应用正常保存的配置。 Server通过`--parallelism`设置默认并行数，默认3；网页从服务读取该默认值，可在启动批次前指定正整数，批次启动后固定。每个实例复用同一套资产，独立连接、进程及结果目录。不按窗口名称或对话框类型强制串行。首轮全部回放、比较和清理完成后，对失败场景最多串行复测两轮，第二轮仅复测第一轮仍失败的项；准备阻塞、pending与取消不当作可重试失败，清理失败阻止后续启动。首轮保留原场景目录，两轮复测分别存入`serial-recheck/<场景>`和`serial-recheck-2/<场景>`，每个场景的`attempts`保留实际执行的各轮结果路径；最终状态来自实际最后一次结果，串行通过另标“复测通过”，不覆盖首轮失败。程序、场景输入或标准发生变化时停止复测。逐项暂停查看使用串行；同主题场景有空位即补充下一个；并行时暂停会停止补位并等待当前运行实例结束，取消会通知当前各实例并阻止后续任务。逐项暂停发生在应用清理后的场景边界；暂停时后台比较继续，点击继续执行下一项。停止保留已完成结果，未运行及未完成项不能通过。录制、待重试保存、回放和配置修改互斥。

回放记录统一展示截图、完整原始 DB 字节比较和产物证据；图片支持并排、本次、原始差异及原尺寸同步缩放拖动。自动结论直接读取正式报告，人工备注单独保存为 `review-decisions.json`，不能改写自动通过或失败。历史记录按当前程序路径与资产目录筛选；场景列表中的最近结果还核对实际程序哈希及场景输入哈希。

回放记录页的“打开已有结果”接受 A 上的结果目录，读取原始报告且不移动或重写它。
已有结果位于默认回放目录或 `--output-root`。CLI 的显式输出通过 `latest-result.json` 登记后同样可以查看。单次回放使用 `execution_result.json`，批次使用 `summary.json` 与 `screenshot_review.json`，不生成替代判定。

关闭网页不影响 A 的任务。停止服务前先停止并保存录制；终端 Ctrl+C 会请求取消回放并等待清理，未保存录制会阻止正常关闭。此能力不包含服务崩溃或机器断电后的录制内存恢复。

`run_gui.sh` 是同一服务的便捷入口，可用 `GPLATFORM_JOURNAL_PYTHON` 选择 Python。旧 Tk 工作台与独立 Tk 对比窗口已退役。

CLI 使用同一个中间层，例如从仓库根目录：

```bash
python3 tools/journal/journal_runner.py \
  --app /path/to/GPlatform.app/Contents/MacOS/GPlatform \
  --run tests/TestAssets/scenarios/gcode_layer_slider/events/Journal.json \
  --output /tmp/gcode-journal-result
```

输出目录必须不存在；省略 --app 则仅连接已有实例，默认端口 9999，不负责其退出。
macOS 的 `.app/Contents/MacOS/` 子程序启动后，中间层等待 LaunchServices 注册，核对本会话
子 PID、bundle 和可执行路径，从同 bundle 的注册实例中精确识别本会话 PID；仅核对身份，不调用 `setfront` 改变前台。它不会用 `open -a` 启动替代实例；身份不符、子进程退出或注册超时按普通启动失败保存诊断。
实例仍须通过应用原有 Ready 和 replayId 握手；连接已有应用及 Windows/Linux 启动流程保持不变。
省略 `--output` 时，从应用的 `journal.paths` 读取 `playbacks`，保存到该目录下的
`session_<时间>_<唯一标识>/`，不依赖运行脚本时的工作目录，也不写入源码目录。
`journal.paths` 的目录规则：录制使用系统应用数据目录；回放不分 Debug/Release，统一使用
可执行文件同级的 `Journal/playbacks/`（`QCoreApplication::applicationDirPath()`），不是 Python 的工作目录。
例如本项目 Release 回放默认位于 `build-Release/GPlatform/GPlatform.app/Contents/MacOS/Journal/playbacks/`。
显式 `--output` 可覆盖本次结果目录；默认目录不可写时明确失败，不静默切换到系统数据目录。
`application.log`、`execution_result.json`、
`verification_report.html`、`reports/` 和 `screenshots/` 均位于同一个会话目录。
回放目录保存执行结果和本次证据，不复制输入 `Journal.json`。
`execution_result.json` 中的 `journalPath`、`journalSha256` 标识源脚本；截图验证从源脚本引用的位置
读取基线并冻结为本次 expected 副本，与该副本比较，原始证据随结果保留。
结果 JSON 的内部路径（reportPath、applicationLogPath、results[].evidence 等）相对该 JSON 所在目录，
outputPath/sessionPath 写为 `.`；读取时用结果文件所在目录解析，不依赖当前工作目录。
HTML 的内部证据链接也使用相对路径，并进行 URL 编码；整个会话重命名、移动或复制时无需修改文件内容。
外部源脚本、基线和保留实例的临时实时日志仍保留外部路径，报告将外部证据标记为 external；
这些外部资源不随结果目录迁移。运行期返回给 GUI/CLI 的绝对路径不变，仅持久化内部引用使用相对路径。
网页将场景结果保留在回放记录内；CLI 显式 `--output` 可用于 CI 自定义位置。
应用启动输出先临时保存，连接后才取得正式目录；若启动失败、尚无法取得目录，
诊断保存在系统临时目录 `journal-startup-failed-*`，并输出实际路径（系统可能清理临时文件）。
`application.log` 是截至测试结束的日志；使用 `--keep-app` 时，后续输出仍写临时日志，
`execution_result.json` 的 `applicationLiveLogPath` 和界面日志会标明位置。
自动启动时省略端口则选择空闲端口，不扫描或结束其他进程。
返回码：验证通过 0，执行或验证失败 1，仅执行/取数无验证 2。
应用日志的 error 行另行提示；JSON 检查通过不代表未覆盖的导入/渲染能力也通过。

### 连接已有应用进行录制/回放

录制和回放统一使用 `gplatform.main` 的 **1382×954 逻辑尺寸**，保留系统原生 DPR。
会话开始时转为普通窗口并固定尺寸，结束时恢复原来的窗口大小、尺寸限制与显示状态。
独立测试进程默认清理 Qt 缩放覆盖，不为改变 DPR 缩小或重启程序。执行输入前核对真实窗口尺寸、
有效 DPR 和屏幕可用区域；窗口放不下或尺寸不符仍失败。

调用方若有明确的测试环境要求，可向 `JournalExecutionSession.run()` 显式传入
`environment_overrides` 字符串字典。该参数在默认清理后应用，仅传给新启动的
测试子进程，并记录到 execution result；不修改调用进程或桌面环境。
未传参数时保留原有行为。通用执行器不决定平台的 DPR 策略，Windows 场景测试的
具体变量由 App `tools/run_acceptance.py` 维护，网页场景测试复用同一入口。

macOS 上由正式执行器启动的回放实例将渲染设置保存到应用数据目录下的 `JournalRender.plist`。
验证设置持久化时，可在 `environment_overrides` 中为连续几次启动指定相同的独立
`HOME` 和 `CFFIXED_USER_HOME`，并设置 `QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM=1` 进行后台回放。

录制检查点保存系统原生 DPR 的原始 PNG，例如 DPR 2 的整窗图为 **2764×1908** 像素。
不在每份新录制 metadata 重复保存窗口/DPR。旧 metadata 中的 DPR/imageSize 作为基线说明接受，
逻辑窗口尺寸仍须符合公共约定；不按旧 DPR 强制缩放、补偿或重启应用。

正式 `verify.screenshot` 保留原有自动比较：尺寸不同仍失败，region/regions 与 blurRadius
仍使用基线的物理像素，容差和通道差不变。跨 DPR 图片不会因为辅助窗口能展示而自动通过。
历史辅助报告可把已知整窗 DPR 1/2/3/4 图片生成 1382×954 的派生视图，并提示差异区域；网页优先展示原始图与严格差异。
原始 expected/actual 和正式 diff 均保留。详情见 App 的 `docs/verification-methods.md`。

先用项目原有方式启动应用，仅在需要不同端口时传 `--ghost-port`：

```bash
./build-Release/GPlatform/GPlatform.app/Contents/MacOS/GPlatform --ghost-port 9999
```

再启动工作台服务，并在网页录制页连接9999端口：

```bash
python3 tools/journal/journal_server.py --application /path/to/GPlatform
```

截图验证依赖 Pillow：

```bash
python3 -m pip install Pillow
```

录制页连接用户已经启动的本地应用，端口放在“连接设置”内。
录制默认保留原有鼠标移动规则，支持悬停和拖动；可填写录制名称。
建议从选择机型、导入模型等初始步骤开始录制；独立回放不会继承当前应用未保存的工程或机型状态。截图候选中的录制提示也可能与回放不同，需审核，不能自动纳入正式标准。
截图与 DB 检查点按需在弹窗中采集，均为候选数据，需人工核对后再纳入正式标准资产。

停止并保存后，可选择录制进行完整诊断回放。回放通过共享 Session 启动独立实例，
不重启当前录制连接中的程序。诊断报告与 App 正式场景验收分开显示。
网页不提供单步、手动 Action、关闭验证、保留测试实例或目录重命名/删除入口；
底层协议和 CLI 原有能力仍由各自入口维护。

录制期间连接中断不会停止 App 录制。保持 A 上的工作台服务运行，重新连接同一录制实例后可继续；
客户端通过现有录制标识确认会话，不会覆盖另一场录制。
停止后若写盘失败，按钮变为“重试保存”。事件、检查点、路径和元数据保留在 Python 内存中，
断线也能重试，不会再次请求 App 停止。保存成功前不能开始新录制；关闭时提示未保存内容。
此能力不包含服务崩溃或进程退出后的持久化恢复。

CLI 连接已有实例回放时，仍使用 `journal.replay.begin` 及 replayId 重启/就绪协议；
应只对专门用于测试、没有未保存工作的应用实例执行。网页录制诊断使用独立实例。

## Journal v2

唯一支持的结构为：

```json
{
  "formatVersion": 2,
  "entries": [
    {
      "atMs": 0,
      "kind": "action",
      "payload": {"code": "ui.ai.close", "params": {}}
    }
  ]
}
```

旧 `m_events` Journal 和旧文本命令不再支持，也不会自动转换。

文件顶层只有 `formatVersion`、`entries`，每条只有 `atMs`、`kind`、`payload`。
名称和描述只写场景/录制目录的 `metadata.json`，不在 Journal 内重复保存。
`atMs` 必须非递减；同一毫秒按数组顺序执行，文件不再包含 `sequence`。
录制内部仍使用应用顺序号合并事件和检查点，保存时排序后移除；加载文件时按数组位置生成
现有通信请求所需的顺序号，不改变 C++ 条目协议。含旧 `metadata`/`sequence` 字段的文件拒绝加载，
不提供旧结构兼容或自动迁移，不修改磁盘上的历史录制。

主动编写的场景统一放在 `tests/TestAssets/scenarios/`，不再给每个场景配专用 Python 脚本。
`capture.db`（dbInstanceId）只保存原始完整序列化字节，不作为验证 Action 发给 C++。
UI 操作不使用业务属性断言或查询接口；默认命名目标找不到或输入派发失败，该条目直接失败并停止执行。
无验证条目的场景明确标为 not_verified，不把“取到了数据”当作业务正确。

仅当后续真实输入依赖异步界面出现时，可在该条 `input.mouse/input.key/input.wheel`
的 payload 中显式设置 `readyTimeoutMs`（1–300000 的整数，且必须有命名 target）。
这是共享 Python 文件执行器的选项，发送输入前从副本移除，默认没有等待。
执行器先确认当前连接 `system.version` 的 `capabilities.namedInputReadiness` 严格为 true；
旧程序或不匹配 SDK 在任何探测、输入派发前清晰失败，不能降级重试。
随后仅通过原 `journal.entry.execute` 的 `checkOnly:true` 请求选项复用目标解析及可输入条件，
不发送事件、不执行 Action、不追加录制、不读取业务数据。不存在、隐藏、禁用或空几何返回
`ready:false`；非法输入和歧义目标立即报错。能力查询、分段收包、探测与轮询共享有限单调截止时间，
等待可取消；超时、取消或通信错误不会发送真实输入。就绪后只派发一次，派发时仍重新检查目标，
若目标已消失则失败，不循环重发。该等待耗时平移回放时钟，保留后续原 `atMs` 间隔。
报告中的 `inputReadiness.waitSeconds/probeCount` 及 `startedAt/readyAt/dispatchRequestedAt`
只是等待与实际输入请求时刻的工具证据，不是业务完成时间；业务成功仍由原 DB、截图及文件检查验证。
异步取消、负向场景不添加此选项，其原语义保持不变。

需要等待明确的观察前置条件时，仅 `verify.screenshot` 和 `verify.db` 可显式添加：

```json
"uiReady": {
  "timeoutMs": 60000,
  "targets": [
    {"target": "slicing.preview.viewSelector", "state": "input-ready"},
    {"target": "gplatform.notificationOverlay", "state": "hidden"}
  ]
}
```

`timeoutMs` 同样为 1–300000 的整数；可指定 `window:{"objectName":"..."}`，默认使用统一回放主窗口。
此能力先严格确认 `namedTargetVisibility:true`，再用原 `journal.entry.execute` 的
`checkOnly:true, observeTarget:{target,window}` 纯描述分支查询，不包含 entry/event，也不构造假输入。
每轮核对全部目标；`hidden` 必须目标确实存在且不可见，禁用、空几何或低透明度不等于隐藏，
`hidden` 的目标缺失、歧义及非法描述不能成为成功。这些条件不查询任意属性或业务状态。
复用同一绝对期限、取消及断流处理，等待耗时记入 `uiReadiness` 并平移后续回放时间。
条件满足后仍只捕获一次并执行原整图比较，不重试截图以寻找匹配帧；能力不匹配时零探测、零捕获。
该例只表示预览控件可输入及指定 Overlay 隐藏，不表示切片业务成功、没有警告或其他通知全部消失。
未标注的截图和其他场景保持原语义。

正常用户提交会卸载控件时，可显式选择第三种状态 `absent`（控件不再存在，
隐藏或禁用但仍存在均不满足）。文件必须在该观察之前含有同一明确
`window.objectName`、同一 `target` 的真实命名 mouse/key/wheel 输入；缺少该依据、
不同窗口或未限定窗口的输入均拒绝加载。共享执行器遇到该输入失败会立即停止，
因此不能以拼错从未出现的目标作为完成条件。该条件仅表示控件生命周期结束，
不保证业务成功；随后 `verify.db` 只读取一次完整原始字节并照原标准比较，
不循环读 DB 等待 expected 相同。取消、超时或能力不匹配时零 DB 读取。

只需要保存截图时，JSON 直接使用已有 `action`，code 为 `screenshot.capture`，
params 例如 `{"filepath":"screenshots/all.png","notify":false}`，不新增截图 Action 或条目类型。
Runner 将相对 filepath 解析到本次结果目录，仍通过现有 Action 请求交给应用保存；
检查文件生成且非空后，把图片链接加入报告。已存在的指定文件会拒绝覆盖，避免旧图被误当本次证据。
这只验证截图落盘；预览画面是否正确仍由调用端判断，已有 `verify.screenshot` 用于可信基线比较。

## 按 objectName 操作控件

现有 `journal.entry.execute` 的 `input.mouse / input.key / input.wheel`
可指定 `payload.target`，按 objectName 搜索窗口 QObject 子对象及 Qt Quick 视觉子项，并直接向控件投递事件。
可用 `window.objectName` 限定窗口；找不到、重名、隐藏或禁用时明确失败，不取第一个候选。
视觉子项遍历覆盖被 Qt Quick 重挂到 overlay 的 Popup 内容。
这不是屏幕命中测试：直接投递不验证控件是否被其他控件遮挡，也不证明业务结果正确。
命名控件的 touch、drag/drop 暂不支持，会明确拒绝，不偷偷改为给窗口发送。

例如向按钮中心发送一次按下（点击还需要对应的 release）：

```json
{
  "atMs": 0, "kind": "input.mouse",
  "payload": {
    "target": "your.button.objectName",
    "event": "press", "button": 1, "buttons": 1
  }
}
```

鼠标的 `position` 是目标控件的**局部逻辑坐标**；省略时取控件中心，scene/global 坐标由应用计算。
拖动使用正常 press → move → release 序列，不直接修改 value 或调用 onMoved。
RangeSlider 的手柄 Rectangle 只负责显示：应向 RangeSlider 发送事件，按下位置取所选手柄中心在 Slider 内的坐标。

GPlatform 首批命名对象：主窗口 `gplatform.main`；层面板 `slicing.preview.layerPanel`；
滑块 `.slider`、手柄 `.firstHandle`/`.secondHandle`、范围文字 `.rangeText`（均以前述面板名称为前缀）。
其他控件需要显式设置稳定 objectName；此改动没有给整个应用所有控件批量命名。

Python 客户端的 `send_mouse/send_key/send_wheel` 支持 `target=`。
`input.mouse` 是录制与手写场景共用的事件类型，不是验证指令。物理录制当前保存窗口和坐标；
命名输入使用同一执行器，但不根据窗口坐标猜测控件或自动补充 target。

例如在已连接的 client 上获取数据：

```python
db = client.serialize_db(db_instance_id)
image = client.capture_screenshot()
```

这两项只是取数。没有预期或可信基线时，不自动判业务通过，也不把本次实际数据当作历史预期。
数据验证仍由调用端负责；此改动没有修改已有截图/DB 取数实现或增加渲染完成等待。

## Session 目录

```text
Journal/
├── recordings/
│   └── session_YYYYMMDD_HHMMSS_mmm/
│       ├── events/Journal.json
│       ├── screenshots/baseline_*.png
│       └── metadata.json
└── playbacks/
    └── session_*/
        ├── execution_result.json
        ├── application.log              # 会话启动应用时
        ├── reports/                     # DB 字节及比较结果
        ├── screenshots/screenshot_*_{actual,diff}.png
        └── verification_report.html
```

Journal 执行器的完整回放和单步结束均只生成一份 `execution_result.json` 和对应 HTML 报告。
App 正式场景还在执行结果外生成 `case_result.json`、`case_report.html` 与汇总。
源文件在本次执行开始时只读取一次，同一份字节用于校验、执行与哈希；执行中修改源文件不改变本轮输入。
中间层先完成连接清理、进程处理及日志收集，再调用公共结果函数生成 JSON/HTML；步骤通过但清理失败，最终结果仍失败。
结果目录命名和创建、普通与外部会话的信息读取均由现有 SessionRepository 统一负责。
只识别会话目录中的 `events/Journal.json`，不回退读取根目录 Journal；回放结果无需包含 Journal。
录制产生的 `events/Journal.json` 和截图基线保留在录制会话中。

## 工作台命令行参数

```text
--application FILE   A 电脑上的实际可执行文件
--app-root DIR       提供统一验收入口与测试资产的 App 源码目录
--assets DIR         显式测试资产目录，默认使用 App 的 tests/TestAssets
--exports DIR        A 电脑上 App 实际导出目录
--output-root DIR    回放结果保存目录，默认沿用应用目录
--listen ADDRESS     网页监听地址，默认127.0.0.1；局域网用0.0.0.0
--port PORT          网页端口，默认8765；录制连接端口在网页中设置
--no-browser         启动后不自动打开本机浏览器
```

截图比较的 `region` 使用物理像素矩形；`regions` 可声明多个矩形的并集，两者互斥，所有目标来自同一捕获帧。
`tolerance` 是选定像素的最低匹配比例。`maxChannelDifference` 默认为0，显式声明时为0～255的整数；仅各RGBA通道差值均不超过此值的像素才匹配。
大小不一致、非法/越界区域仍失败；报告保存冻结的expected、完整actual及只标红选定区域不匹配像素的diff。
这些参数应由具体目标及重复性测量确定，并保留错误状态对照，不能通过更新参考或放宽参数隐藏错误输出。

For stochastic 3D render checkpoints only, `blurRadius` optionally applies a Gaussian filter of 0–2 physical pixels to both comparison inputs before the declared channel limit and region mask. It defaults to 0. Full expected/actual PNG evidence stays unfiltered, and the report records the radius. Establish scene-specific limits using repeated correct frames and genuine wrong states; do not use filtering for UI text or numeric fields.

### DB 数据读取

App 通过既有 `setDbStateProvider()` 提供一个 DB 属性读取入口。
`serialize_db(id)` 沿用原有协议；返回的格式标识用于核对数据，不能代替字节比较。
`capture.db` 只保存数据，`verify.db` 必须与已审阅标准比较才能完成 DB 验收。
