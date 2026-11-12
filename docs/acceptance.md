# 应用验收架构

## 仓库与所有权

测试资产唯一来源为 [GeoPlatform-TestAssets](https://github.com/dezbracaty/GeoPlatform-TestAssets)。
App 在 `tests/TestAssets` 以子模块固定提交；完整宿主使用 `App/tests/TestAssets`，不再保存另一份模型或场景。
普通构建按需初始化测试资产；完整宿主启用 `GPLATFORM_BUILD_TESTS` 时需要该固定版本。
资产仓库只拥有模型、Journal、metadata、预期、基线、格式和确定性生成器。

App `tools/acceptance.py` 拥有业务验收编排与最终结论；`tools/acceptance_checks.py` 拥有产物检查。
App `tools/journal/` 是 Python 客户端、网页服务、启动/握手、输入回放、DB/截图比较、清理和
执行报告的唯一实现，相关 Python 测试位于 `tests/journal/`，与 App 提交一起版本化。
SDK 的 C++ JournalSystem 提供应用端服务、输入执行和原始数据接口；Python 工具不随 SDK 发布。
主仓库 `scripts/run_journals.py` 只是共享入口的兼容转发，不再在内存中替换执行器源码。
完整宿主私有 C++ 回归继续留在 `Core/tests`，只改为读取固定资产。
原生切片回归的 catalog、模型、配置和三平台 G-code 基线由资产仓库 `slicing_regression/` 维护，
直接算法调用与比较实现仍在宿主 `scripts/`；它们沿用原生回归格式，不转换为 GUI Journal。
确定性模型生成器随资产维护，宿主旧生成入口仅转发。Journal 状态提示场景也统一从
`scenarios/journal_status/` 读取。历史记录位于资产仓库 `docs/legacy-host/`，不参与当前通过判定。

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

校准的保存/采纳结果可声明必需的 `calibration-observation` 补充检查，`artifacts` 为 `[]`，
独立预期放在 `expected.calibrationObservations`。执行器只读取本次运行开始后由应用生成的
校准记录，核对任务身份、参数、观察值、目标材料及采纳状态，并归档到结果的
`artifacts/calibration/`。每轮预期对应一份新记录；旧记录不能使保存操作通过。
完成弹窗中的记录文件名包含随机 UUID，不适合固定像素标准；保留保存输入，关闭该弹窗后
拍摄稳定的后续界面，保存结果由记录检查验证。其余截图仍须整图精确比较。
模型通过 `namespace/id` 引用。Journal 实际导入路径必须与声明一致，且不能逃逸资产目录。
生产 Action 创建的模型记录 Action 和完整参数，并与 Journal 核对。
模型几何预期来自解析构造；任务预期来自明确业务要求，不能直接采纳当前错误输出。

## 统一入口与处理顺序

正式 `--application`／GUI 新进程回放由 `JournalExecutionSession` 统一设置
`GPLATFORM_JOURNAL_SESSION=1`。`SliceSettingsBridge` 因此不读取或保存用户记忆的
机型、喷嘴、打印机／工艺／耗材选择；启动选择为空时从 `Flashforge AD5X / 0.4`
默认配置开始，切机时也不读取用户记忆的该机型配置。用户日常保留A500不会成为此入口的起始机型。
场景中的P3、A500或项目自带机型由原有真实操作／工程导入建立，不能把最终目标机型统一提前到所有场景开头。

这层保护只在正式执行器启动的新进程中建立；手动在普通程序内回放或连接已有进程，
不能假设已忽略当前机型。此类场景必须先通过已有业务输入建立并核对其声明的起始状态。
声明 `applicationEnvironment` 的场景不允许仅连接已有程序；metadata中的说明文字本身不是切机动作。
机型、喷嘴和预设下拉优先按控件objectName定位，键入完整显示名称并用Return确认，随后以已有DB／截图检查实际选择。
具名鼠标输入可不写position，由执行器取控件中心；不得用列表第N行的屏幕坐标代替具体选项身份。
既有P3等场景已采用文字＋Return。3D视口拾取及不能文字检索的虚拟图例，应按各自真实交互处理，不混同为机型下拉。

不要为已有统一启动保护给每份Journal机械添加重复切机，从而改变首次打开、取消切机或工程恢复的覆盖。

`tools/run_acceptance.py` 同时支持完整宿主和独立 App，默认加载本 App 的 `tools/journal/`，
不依赖完整宿主或另外配置工具目录。`--journal-tools` 仅用于显式选择开发中的另一份工具，
实际工具来源仍写入报告。工作台默认 App 根目录由自身位置确定；CLI、工作台和候选采集共用实现。
宿主旧 `ThirdParty/JournalSystem/tools` 仅保留命令行转发，不再维护一套模块。

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

所有活动场景统一放在 `scenarios/<id>/events/Journal.json`，旁边必须有正式 metadata。
CLI、工作台和 CI 共用 `session_repository.discover_scenarios()`；发现其他位置的 Journal 或缺少
Journal 的顶层 metadata 时直接报错，不能静默漏项。已登记场景的 `windows/events/Journal.json`
仅保留为历史平台版本，不作为 Mac mini 的另一个入口。输入文件放入模型目录并登记目录清单。

```text
<run>/summary.json
<run>/report.html                      批次结果与证据索引
<run>/screenshot_review.json           辅助逐图对照清单
<run>/review-decisions.json            可选人工备注，与自动结果分开
<run>/<case>/execution_result.json       Journal 原始结果
<run>/<case>/verification_report.html    Journal 原始报告
<run>/<case>/application.log
<run>/<case>/screenshots/
<run>/<case>/artifacts/                  本次实际产物
<run>/<case>/reports/                    业务检查证据
<run>/<case>/checks.json
<run>/<case>/case_result.json            场景业务结论
<run>/<case>/case_report.html
<run>/<case>/review/                   冻结原图与辅助派生图
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

网页默认场景目录也指向资产仓库。勾选回放通过上述共享入口执行指定场景；
录制页的诊断回放显示 Journal 原始验收，不能替代正式场景的 `case_result.json`。

工作台默认将结果保存在所选可执行文件旁的 `Journal/playbacks/session_…/`；macOS 即
`GPlatform.app/Contents/MacOS/Journal/playbacks/`。CLI 的 `--output` 覆盖本次保存位置。
批量结果在批次目录下按场景 ID 分目录。标准图片不从历史回放目录选择，而由每个检查点的
`payload.baseline` 相对源 Journal 解析；正式场景通常引用 `baselines/screenshots/<case>/`。
比较器先冻结为本次 `screenshots/screenshot_<序号>_expected.png`，再与同目录
`screenshot_<序号>_actual.png` 比较并生成 `screenshot_<序号>_diff.png`。

批量回归使用 `--cases all`，串行执行全部场景，逐项保存状态；取消后保留已完成和未运行项。
多场景导出自动隔离到 `<exports>/<批次目录名>/<case>/`，避免相同文件名相互覆盖；
单场景仍直接使用传入的导出目录。结果目录必须新建，不能复用上次目录。

录制、场景与批次结果统一由 Journal 网页工作台查看：

```sh
python -m pip install -r App/tools/requirements.txt
python App/tools/journal/journal_server.py --application /path/to/GPlatform
```

服务电脑负责执行与保存结果，其他电脑通过浏览器访问。默认只监听本机；局域网启动可加 `--listen 0.0.0.0`，使用服务启动时显示的6位数字连接码登录。无需 Tk。详见 [工作台说明](../tools/journal/README.md)。

CLI 的自定义输出目录通过默认回放目录下的 `latest-result.json` 记录位置；该文件只用于定位，结论仍读取原始结果。最近结果包含单场景、整批和直接回放。网页支持截图原尺寸同步缩放拖动、原始差异、DB完整字节及文件证据；缺图、比较中、未运行和清理失败保留真实状态。严格结果不能人工改通过。候选人工意见另存 `review-decisions.json`，绑定原始图像哈希。

增加场景先确定数据验收还是画面验收，再补对应标准 DB 或标准截图，复用现有比较代码。
截图基线需要独立审批与平台/DPR/主题来源。实机结果不属于本轮场景验收。

回归时间轴应移除录制时的停顿，分别核对键鼠输入、动画、后台导入和切片所需的短间隔。
慢检查造成回放落后时，执行器顺延时间轴，保留后续输入间隔，不通过突发派发追赶原时刻。
同一状态已有 `verify.screenshot` 时，复用其原始实际图作为诊断证据，不再额外安排 `screenshot.capture`。
同一状态、同一基准的重复截图可合并为一次采集；正式比较不读取场景的区域或容差来缩小门禁。
不同状态或重复观察同一区域的时序检查保持独立。
正式截图报告直接复用执行器的标准图、实际图、差异图及结果，不再重复执行感知比较或 OCR。
批量回归在后台串行生成截图辅助报告，前台继续下一场景；退出批次前收齐后台任务并更新最终报告。截图像素比较在独立后台线程池执行，前台截图落盘后继续回放和启动下一场景；只有整批结束才等待剩余比较。结果直接回填原步骤，pending 不能算通过。
调整时序或移除重复采集后，保持原标准图与 DB 预期，核对 `dbBaselines.entryIndex`，并实际回放验证。

## 3MF 的业务验收与样本回归

普通界面的文件选择、拖放和模型库 3MF 导入均由 `ProjectImportHandler` 决定导入方式；
未指定 `intent` 时先显示统一的“打开工程／导入模型／取消”弹窗，`.gcode.3mf` 也不预选方式。
“打开 3MF”按钮只筛选文件，不预选工程模式。脚本显式 `intent` 原样传递；
`project.open` 保留为显式打开工程的 API 别名，调用者提供 `intent` 时以该值为准。

3MF 保留 16 个业务场景，复用同一 catalog、metadata v3、正式 Journal 和共享执行入口。
文件选择器、中文路径、拖放、工程替换、原生确认、取消重试、耗材槽、实例、撤销重做和视觉恢复按独立行为组织，
统一导入场景 `three_mf_unified_import_workflow` 在同一进程连续验证取消、拖放追加和文件选择器打开工程，逐阶段比较 DB；
不为每个输入文件生成一套 GUI 场景。覆盖矩阵见资产仓库 `docs/3mf-scenarios.md`。

56 个官方原件（22,248,071 字节，51 种内容）位于 `models/three_mf_official/`，原始字节与重复来源路径均保留。
manifest 固定上游提交、下载链接、大小和 SHA-256，许可来源另存 `licenses.json`。
批量读取使用已有 `libslicer_api_tests` 的 `[project-corpus]`，原生行为对照使用
`orca_plater_tests` 的 `[project-orca-reference]`；两者通过 `LIBSLICER_PROJECT_CORPUS` 指向样本目录，
分别遍历 Geometry/Project 入口。前者允许原生拒绝并检查活动配置不被读取污染，后者对照原生读取成功/拒绝、
有效配置及几何转换；这两项不能替代真实 App 的文件选择、交互与视口验收。

每个业务场景声明 DB 或截图作为主要目标；所有截图都附加强制整图比较，默认仅排除统一 FPS 按钮区域；另有3张原生导入询问图按明确批准的局部动画例外处理，规则见 `verification-methods.md`。`capture.db` 只是原始数据采集。
`verify.screenshot` 和 `screenshot.capture` 必须绑定独立标准，不能仅凭保存图片满足门禁。单个配置 DB 不代表文档对象图、实例数、角色、
历史或完整 Backend 配置；这些范围必须由实际对应对象的比较或原生专项分别证明。
尚缺断言的场景保持 `not_verified`，不能因操作完成或输出文件检查通过而升级。

App 的注册 CTest `Model3MFIntegrationCheck` 经实际 `file.import`、`import_model`、模型库和导入 Handler
核对属性序列化之外的所有权、共享关系、VTK 网格及部件局部矩阵，并检查工程替换、
追加图的撤销重做、有效切片输入和 Backend 层高编辑／重开恢复。
这是 App 原生集成补充，不替代 Journal 门禁、真实输入或视口检查。
两个工程回归测试的源码和注册位于 `tests/`；完整宿主通过 `GPLATFORM_BUILD_TESTS`、
独立 App 通过 `GPLATFORM_BUILD_SDK_CHECKS` 构建同一目标。
Python 验收合同测试的依赖用 `python -m pip install -r App/tests/requirements.txt` 安装
（独立 App 根目录使用 `tests/requirements.txt`），其中包含真实 metadata 的 JSON Schema 校验。

```sh
# 查看场景 ID 和标准准备状态
python App/tools/run_acceptance.py --list
# 按明确的 ID 列表执行，使用新结果目录和实际程序
python App/tools/run_acceptance.py --cases <comma-separated-ids> --application <actual-executable> --output <new-run-directory> --exports <new-export-directory> --settings-file <actual-App-INI>
```

文件选择和拖放通过正式 Qt 输入回放。`${inputDirectory:MODEL_ID}` 和 `${inputUri:MODEL_ID}`
仅绑定已声明且哈希正确的仓库样本；`${exports}` 绑定保存窗口的位置字段。中断时分别报告计划、完成和未运行项。
Journal 默认录制和回放使用 1382×954 逻辑窗口，保留系统原生 DPR；DPR 2 的整窗截图为
2764×1908 像素。会话结束恢复原窗口大小、限制及显示状态；窗口放不下时明确失败。
默认不强制 DPR=1，也不通过缩放补偿重启程序。既有基线的 DPR 不作为启动时强制设置。
批次入口在每个场景启动前按 `preconditions.theme`（`light`／`dark`）准备现有 App INI 的
`darkMode` 配置；结束、失败或取消后按原始字节恢复，原先不存在的 INI 会删除。
Windows/macOS 自动定位该 INI，其他平台使用 `--settings-file`。这是 App 验收侧的环境准备，
不增加 Journal 主题协议，也不修改系统主题。
正式截图验收仍比较原始图像，尺寸不同会失败；对照窗口的派生图可跨已知 DPR 展示，
但不会代替自动验证。更新标准仍需核对独立预期、保留原始证据，并在目标环境的新进程中复跑。

App `tools/run_acceptance.py` 拥有 Windows 场景测试的启动环境策略：CLI 和网页场景
测试均通过此入口，对新测试实例显式传入 `QT_ENABLE_HIGHDPI_SCALING=0`、
`QT_SCALE_FACTOR=1`、`QT_FONT_DPI=96`，用于建立 DPR 1 的 Windows 标准。
候选采集也走同一入口。桌面缩放、注册表和用户环境变量不变。
macOS/Linux 不传覆盖参数，保持原来的原生 DPR 行为；macmini 资产和基准不变。

JournalSystem 的 `environment_overrides` 是通用子进程环境参数，不包含 Windows
或 DPR 专属策略。优先级为：继承环境、既有场景环境、默认缩放清理、显式覆盖；
未传此参数时与原实现一致。覆盖仅适用于由 Session 启动的新实例，不能修改已有 App。
批次 summary、case provenance 和原始 execution result 记录本次显式覆盖值。
实际窗口信息仍由既有 Journal 接口读取；DPR 和原始 PNG 尺寸应与新标准一致，
不对原始图像做缩放补偿，也不把截图采集成功当作业务验收通过。

### G-code 与文件包补充检查

`three_mf_complete_workflow` 保留受控双盘工程的编辑、逐盘切片和导出，使用 `project-gcode`
核对实际沉积、源盘局部范围、层数、声明的包内配置、包内外精确 G-code 字节及必需 MD5。
盘切换使用正式 `plate.activate` Action；参数编辑和导出仍走真实输入，不宣称覆盖盘选择界面。
这是补充门禁，不重复验证对象图、配置恢复或撤销重做，也不能满足缺失的 DB/截图门禁。
其他任务原有的 `gcode-job` 同样仅作补充；官方原始样本不再各自维护 GUI 导出长链。

输出检查支持原生 XY 平面、相对 I/J 圆心的 G2/G3，核对端点及内部圆弧极值；不支持的
P/K/R、其他平面、绝对圆心和非平面沉积明确拒绝。均匀层的最少层数与 Z 格点由独立源事实、
声明的 UI 层高和原内核中点规则推导；薄模型仍必须生成正确的一个初层。

截图在后台按原尺寸整图逐像素比较；历史区域和容差不再缩小正式门禁。原生对照及输出检查分别报告，不据此宣称
选择状态、全视口外观、完整刀路等价、设备行为或实物打印已经验收。

## Windows 基准采集

复用本入口增加 `--capture-candidates`，在原有 DB、截图检查点采集候选。原场景和标准保持不变，
运行目录 `.declarations/` 保存物化后的执行 Journal；输入、时间点和 entryIndex 不变。
DB 使用已有 capture.db，截图使用已有 screenshot.capture，并进入同一截图复核报告。
执行失败仍报 failed，候选完成报 not_verified，不会自动更新标准。

逐检查点核对业务目标后，将审核通过的原始文件写入对应平台资产分支的 `baselines/db/<case>/`
或 `baselines/screenshots/<case>/`，同步 `scenarios/<case>/` 的标准引用、来源、哈希和审核依据，
再用原有 verify.db/verify.screenshot 完整回放。不另建顶层 `TestAssets/windows` 验收入口；
macmini 分支各场景中的 `windows/` 是保留原字节的历史资产。候选目录不能作为通过结果，CI 拒绝 candidateCollection。

包含部署资源路径的 DB 场景可声明 `deploymentResources`（`resources/...` 相对路径到 SHA256 的映射），
并在 `preconditions.applicationDirectory` 保留采集目录。物化时先验证当前应用资源哈希，再绑定标准中
唯一对应的 `string.utf8` 路径及长度；其他字节保持原样，实际 DB 始终完整比较。原始标准及运行 JSON
分别保留哈希。资源缺失、内容变化、未引用或重复路径都报错；未声明此字段的场景行为不变。
