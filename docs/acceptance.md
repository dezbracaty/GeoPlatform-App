# 应用验收架构

## 仓库与所有权

测试资产唯一来源为 [GeoPlatform-TestAssets](https://github.com/dezbracaty/GeoPlatform-TestAssets)。
App 在 `tests/TestAssets` 以子模块固定提交；完整宿主使用 `App/tests/TestAssets`，不再保存另一份模型或场景。
普通构建按需初始化测试资产；完整宿主启用 `GPLATFORM_BUILD_TESTS` 时需要该固定版本。
资产仓库只拥有模型、Journal、metadata、预期、基线、格式和确定性生成器。

App `tools/acceptance.py` 拥有业务验收编排与最终结论；`tools/acceptance_checks.py` 拥有产物检查。
App `tools/journal/` 是 Python 客户端、GUI、启动/握手、输入回放、DB/截图比较、清理和
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
模型通过 `namespace/id` 引用。Journal 实际导入路径必须与声明一致，且不能逃逸资产目录。
生产 Action 创建的模型记录 Action 和完整参数，并与 Journal 核对。
模型几何预期来自解析构造；任务预期来自明确业务要求，不能直接采纳当前错误输出。

## 统一入口与处理顺序

`tools/run_acceptance.py` 同时支持完整宿主和独立 App，默认加载本 App 的 `tools/journal/`，
不依赖完整宿主或另外配置工具目录。`--journal-tools` 仅用于显式选择开发中的另一份工具，
实际工具来源仍写入报告。GUI 默认 App 根目录由自身位置确定；CLI、GUI 和候选采集共用实现。
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

GUI 默认场景目录也指向资产仓库。“运行场景”和“批量回归测试”均使用上述共享入口；
录制页的诊断回放显示 Journal 原始验收，不能替代正式场景的 `case_result.json`。

GUI 默认将结果保存在所选可执行文件旁的 `Journal/playbacks/session_…/`；macOS 即
`GPlatform.app/Contents/MacOS/Journal/playbacks/`。CLI 的 `--output` 覆盖本次保存位置。
批量结果在批次目录下按场景 ID 分目录。标准图片不从历史回放目录选择，而由每个检查点的
`payload.baseline` 相对源 Journal 解析；正式场景通常引用 `baselines/screenshots/<case>/`。
比较器先冻结为本次 `screenshots/screenshot_<序号>_expected.png`，再与同目录
`screenshot_<序号>_actual.png` 比较并生成 `screenshot_<序号>_diff.png`。

批量回归使用 `--cases all`，串行执行全部场景，逐项保存状态；取消后保留已完成和未运行项。
多场景导出自动隔离到 `<exports>/<批次目录名>/<case>/`，避免相同文件名相互覆盖；
单场景仍直接使用传入的导出目录。结果目录必须新建，不能复用上次目录。

GUI 的“显示最近批量结果”打开本地 Python 窗口，也可手动打开已有批次：

```sh
python -m pip install -r App/tools/requirements.txt
python App/tools/review_results.py <run-directory>
```

Python 需带可用的现代 Tk。每组卡片对应一个截图检查点；卡片区滚轮上下切换组，
hover 展开标准图和差异提示图，点击后在同一窗口下方显示标准／本次／差异详情。
详情支持同步缩放、拖动和切换完整像素差异；DB 结果、输入失败、缺图及未运行项单独保留状态。
原始图像哈希绑定可选人工备注，另存 `review-decisions.json`，不会改写 `case_result.json` 或自动判定。
对照提示、OCR 或备注不可用不改变自动结果；工具错误会保存在辅助诊断字段中。

增加场景先确定数据验收还是画面验收，再补对应标准 DB 或标准截图，复用现有比较代码。
截图基线需要独立审批与平台/DPR/主题/容差来源。实机结果不属于本轮场景验收。

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

每个业务场景只选择 DB 或截图门禁。`capture.db` 只是原始数据采集，只有正式 `verify.db` 或
`verify.screenshot` 及相应独立标准才能满足门禁。单个配置 DB 不代表文档对象图、实例数、角色、
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

App `tools/run_acceptance.py` 拥有 Windows 场景测试的启动环境策略：CLI 和 GUI 场景
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

截图只覆盖 Journal 声明的目标区域。诊断截图、原生对照及输出检查分别报告，不据此宣称
选择状态、全视口外观、完整刀路等价、设备行为或实物打印已经验收。
