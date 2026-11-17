# 场景验收：DB 或标准截图

## 两种方式

| 类型 | 场景目标 | 比较依据 |
| --- | --- | --- |
| `db` | 导入、编辑、配置、涂色、撤销重做等数据结果 | 正式 `verify.db` 比较实际 DB 序列化与标准 DB |
| `screenshot` | 面板、弹窗、布局、图层显示和渲染外观 | 正式 `verify.screenshot` 比较实际画面与标准截图 |

每个场景声明一种主要验收方式；其中每个截图步骤仍必须通过标准截图比较。
DB 和截图检查共同决定结果，DB 通过不能抵消截图失败或缺少截图标准。
原生 CTest、G-code 文件检查和校准观察记录检查保留各自证据，不代替 DB 或标准截图验收。
自动场景不以“人工专项”作为验收方式。标准在建立时核对独立预期，后续由正式比较器自动判定；
未保存标准或未配置对应比较点的场景就是未完成，不能计入已验收覆盖。

## 场景定义

metadata v3 的 `verification.requiredMethods` 只允许单项 `db` 或 `screenshot`。
`acceptanceTarget` 写明具体检查对象和目标，`Journal.json` 保留正式输入及检查点。
每个场景保存自己的标准数据，共用 Journal 的获取和比较代码，不配置脚本路径或自定义命令。

录制标准前先审核目的、实际步骤和观测能力，逐项结论见
[Journal 自动比较清单](../tests/TestAssets/docs/scenario-acceptance.md)。
资产预检通过只证明格式和引用可用，不证明选用的 DB 或截图能验证业务目标。
先确定具体的独立预期、必要的中间状态和错误对照，再采集标准；不按场景数量机械补基线。

DB 检查点沿用 `verify.db`，格式为 `transdb.cereal-portable-binary.v1`。
当前接口按实际 DBInstanceID 获取单个对象的完整属性存储，不代表整个文档关系图。
现有 `app.db.serialize` 按 ID 返回该 DB 的原始属性字节；App 不另行定义一份工程字段投影。
多个 DB 使用多个检查点。该接口不自动遍历文档，也不代表已覆盖 VTK 网格等属性存储之外的数据。
有准确标准时才使用 `verify.db`；只有 `capture.db` 的场景仍缺少 DB 验收，不能判为通过。

异步动作被接收不代表业务完成；结果通过已有 DB 或截图比较检查，缺少标准时保持未验证。
`file.import` 的分派成功不代表具体 `project.import` 任务已完成；终态断言也不能替代 DB 门禁。

标准 DB 保存在资产仓库 `baselines/db/<case>/`；`dbBaselines` 将文件绑定到 Journal 内的 expectedSerialized。
入口校验两者相同，并记录文件哈希。对象 ID、初始化配置及关联对象必须随场景说明，禁止把未识别对象当目标。

核对实际序列化内容：`SlicingConfigDB` 不包含校准的全部活动参数和观察记录，
不能以其 `ActiveRevision` 变化证明流量、PA、温度或回抽值正确。
`ToolpathPreviewDB` 的运行期层、线段和统计也不能由普通属性字节推断。
一个对象的标准无法证明文档对象数量、对象不存在或整个归属图；这些目标复用对应原生检查，Journal则明确其可比较目标。
现选方法无法观测主要目标时，先重写验收定义，不能通过录入当前状态制造覆盖。

截图检查点沿用 `verify.screenshot`，引用资产仓库 `baselines/screenshots/<case>/` 中的标准图片。
录制与回放固定 1382×954 逻辑窗口，保留系统原生 DPR；主题、语言和适用平台仍由场景约定。
基线的 DPR/imageSize 记录其来源；宽高必须与实际截图一致，不做缩放补偿。
`verify.screenshot` 和 `screenshot.capture` 都使用 `payload.baseline` 绑定独立认可的标准图，
路径相对源 Journal。Action 的 baseline 仅由 Python 执行器消费，不发给 App。
未绑定标准的截图不能通过，也不会自动把本次图片变成标准。

正式回放统一比较原尺寸整图的 RGBA 像素，默认仅排除右上角收起状态的 FPS 按钮区域；下述明确批准的导入询问动画图另有局部例外，参与比较的任一像素不同即失败。
1382×954 逻辑窗口的 FPS 排除框在 macOS/Linux 为 `[1260, 72, 110, 36]`，
Windows 为 `[1260, 78, 110, 36]`：Windows 标题和命令区域高 6 个逻辑像素，因此同一按钮整体下移。
两者排除面积相同，均包含按钮宽度变化及阴影，按原生 DPR 映射到物理像素。
其他尺寸的窗口截图不隐式排除；FPS 展开详情不属于排除范围，窗口尺寸不一致仍失败。
原始标准图与实际图不修改；结果记录排除框和参与比较的像素数，diff 只标记参与校验的差异。

用户批准的原生导入询问动画例外，仅用于本机 macOS 回放、1382×954、DPR=1 的以下标准图：
`three_mf_filepicker_geometry/import-mode.png`、`three_mf_native_decision_cancel/native-question.png`、
`three_mf_target2_geometry/native-options.png`（均位于 `baselines/screenshots/`）。
除 FPS 外，仅排除底部旋转图标 `[736,894,24,24]` 和不定进度内容 `[766,913,300,5]`。
范围依据 `GlobalPrintCommandBar.qml` 的16px旋转图标完整包络和300×5进度内容；不包含状态文字和进度背景上下缘。
Windows omits these animated import-question screenshots; native choice inputs and final-state checks remain.
共享比较器按上述平台和资产路径固定识别，不接受任意屏蔽框；其他阶段、其他尺寸/DPR不采用此例外。
弹窗文案、按钮、焦点、警告、模型及其余画面继续精确比较，不增加等待、不改变动画或产品行为。

历史 `region/regions`、`tolerance`、`maxChannelDifference`、`blurRadius` 字段仍可读取，
但不能缩小或放宽正式验收。原标准图片保持不变；diff 标记整张原图的差异。
截图不能用黑图、其他窗口或错误输出作为标准。

截图保存后立即提交后台线程池，前台继续输入和下一个场景，不等待图片解码或像素比较。
比较结果直接写回对应步骤的 comparison 字段；未完成为 pending，完成为 passed/failed。
整批回放结束后收齐比较结果，再确定最终状态及退出码。缺标准、尺寸不一致和比较异常均失败。

Windows/macOS 的字体、原生 DPR 和 Qt 版本不同，自动验收标准不能直接混用。本轮主 `Journal.json`
使用 metadata 声明的 macOS 环境；原有 Windows 标准按原字节保存在对应 `windows/`
目录，操作文件为 `windows/events/Journal.json`，原环境声明保存在 `windows/metadata.json`。
这些历史平台文件不会加入当前 macOS 批次，也不因 macOS 通过而获得 Windows 通过结论。
需要复跑时直接使用已有 `journal_runner.py --run <windows/events/Journal.json>`，
正式 session 会读取同目录上层的原始环境声明，不增加平台专用执行器。

数值输入结束后应通过正常输入移走焦点，避免闪烁光标进入标准。
除上述明确批准的局部动画例外外，不能通过新增排除区域、涂改标准或放宽阈值消除差异。
macOS/Windows 使用应用窗口自身的渲染帧采集，避免桌面窗口切换动画和其他应用混入图片。

## 本轮建立标准的过程

先执行场景并采集候选 DB/截图，按明确业务要求核对，再保存标准文件及来源、哈希和审阅依据。
随后在新的 App 进程中重放，使用已有比较器验证；同时用有意错误的 DB/图片确认比较能拒绝错误结果。
没有标准时继续补标准，不把“没有标准”当作验收完成，也不把候选输出自动批准。

`capture.db` 只采集 DB，不能单独产生通过结论。`screenshot.capture` 在正式回放中必须绑定并比较标准；仅显式候选采集模式禁用比较，并保持 not_verified。
`--list` 同时列出资产状态、验收类型与缺少的检查点；当前结果以实际运行的 case_result.json 为准。
历史六类方法的 v2 报告保留原文，不能解释为 v3 的 DB/截图通过。

## 结果

执行、清理和该场景的全部正式比较通过才判 passed；实际不符判 failed。
缺少检查的诊断回放仍为 not_verified，错误输入或缺文件在启动前明确报错。
DB、图片的 expected/actual/diff 和原始 Journal 报告保存在统一运行目录。
截图比较在抓取实际画面前将标准图复制为本次结果的 `screenshot_<序号>_expected.png`，
并与该副本比较；后续更新仓库基线不会改写历史报告的 expected。
DB 同样保存每个检查点的 expected/actual 原始字节及比较哈希。
P3 的原有文件检查标为 supplementalResults，不能让缺少 DB 比较的场景变成 passed。

场景逐项分类、目标与现有检查点见资产仓库 `docs/scenario-acceptance.md`。

## 批次截图对照：辅助查看

App 批次系统在正式回放之外生成 `screenshot_review.json`，由 `tools/journal/journal_server.py` 的网页工作台
展示每个检查点的标准图、本次图和差异提示。它属于批量回归结果查看能力，不增加 Journal
验证方式，不把 `verify.screenshot` 改成只采集。没有标准、缺图、尺寸不兼容和未运行均明确显示。

正式自动比较始终使用完整原图进行严格像素比较，原始尺寸不一致即失败。
新回放的结果查看直接复用执行器的比较结果与差异图，不再重复执行感知分析或 OCR。
以下能力保留用于历史报告及候选图片的辅助查看，只生成派生视图，不改变自动门禁或原始证据：

- 对已知整窗尺寸（1382×954 的 1/2/3/4 倍）使用 LANCZOS 生成 1382×954 视图；
  区域从基线物理像素按比例向外取整。相同尺寸的局部图可直接展示，未知的不同尺寸不拉伸。
- 保留全部归一化后 RGBA 像素差异，并可切换到“原始像素差异”；这张图也属于派生图，
  原始分辨率证据仍在各检查点的 expected.png／actual.png 和 Journal 正式报告中。
- `visual-hints-v1` 以 OKLab 色差、最多一个标准像素的局部邻域及局部密度生成提示，
  不做全局配准或几何拟合。红色为重点核对候选，淡橙色为较弱差异提示，均不表示通过。
- 本地 OCR 可降低同位置同文字的重复提示，并框出识别到的数字变化候选。识别可能出错；
  macOS 使用 Apple Vision，其他平台可选 Tesseract，不可用时明确提示，不能据此排除真实错误。
  NumPy 辅助计算失败时退回 Pillow 像素差异；辅助图或清单失败只记录错误，正式结果不变。

派生图和提示记录策略版本、源图尺寸、比较范围与 OCR 状态，原始图像保持字节及 SHA-256。
仅上述历史报告及候选视图提供人工“可接受／缺陷”及备注，写入独立 `review-decisions.json`，绑定批次和两张原图哈希；
未审核不会降级自动 passed，人工可接受也不会升级自动 failed。此窗口不更新仓库基线。
严格回放的查看器只显示执行器判定，不提供人工改通过入口。
