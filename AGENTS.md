# GPlatform 应用开发

从 `README.md` 确认独立 App 的源码与 SDK 边界。所有项目技能与引导文件均随 App 维护；完整源码宿主可以直接读取同一份文件。

下文 `APP_ROOT` 指包含本文件、`src/`、`qml/` 和 `CMakeLists.txt` 的目录；`HOST_ROOT` 仅指同时包含该 App、`Core/` 和 `cmake/sdk/` 的完整源码宿主。独立 App 不假定存在 `HOST_ROOT`。

## 技能选择

- 所有代码变更先读取 `.agents/skills/gplatform-bugfix/SKILL.md`，执行其中的修改范围检查点；bug 修复与回归处理同时执行完整的问题定位与验证流程，再按涉及模块组合下列技能。
- 页面、面板、弹窗、主题、QML 与 Bridge 接线：`.agents/skills/gplatform-ui/SKILL.md`；先查现有页面和 SmoothUI 组件，优先复用控件、主题与交互，只补需求确有必要的能力。
- DB、事务、Action、撤销重做、业务注册：`.agents/skills/gplatform-db-action/SKILL.md`。
- 切片请求、配置、SDK、内核、连续纤维与预览：`.agents/skills/gplatform-slicing/SKILL.md`；算法库能力先定义输入输出契约，再查原始 GUI/内核调用链，优先提取复用并做必要协议封装。
- 配置、编译、运行与相关测试：`.agents/skills/gplatform-build/SKILL.md`。
- VTK/Filament、DBSync、拾取与帧交换：`.agents/skills/gplatform-rendering/SKILL.md`。
- 真实程序 Journal 编写、回放与验收：`.agents/skills/gplatform-journal/SKILL.md`。
- SDK 导出、独立 App 消费与交付：`.agents/skills/gplatform-sdk-delivery/SKILL.md`。

按任务组合；小型文本或布局修改不自动升级成完整设计、审查或性能分析流程。
处理 `qml/`、`src/Bridge/`、`src/Interaction/` 时先读对应目录的 `AGENTS.md`。

## 严格构建要求

- 所有 build 必须先执行本项目的 `cmake --preset <preset>`，再执行同名的 `cmake --build --preset <preset>`。完整宿主、独立 App、Orca 单独构建分别使用自己的正式入口与目标。
- 禁止用临时 CMake 工程、手写编译/链接命令、手工复制 DLL、替换旧交付库等方式绕过标准构建和部署；正式 preset 的配置参数及已注册测试目标可按任务使用。
- 源码修改后必须按标准流程重建实际被测程序，并核实运行库和资源来源。直接库调用、库探针或旧产物不能替代 App 流程验收；分别说明编译、加载、启动和业务结果。

## 源码与依赖

- 所有本次改动先进入功能分支；涉及修改的 App、源码库及需更新的 SDK、测试资产使用同名功能分支，依赖以完整 SHA 固定。完整宿主的[统一提交与合并流程](https://github.com/dezbracaty/GPlatform/blob/feature/gplatform-ui/docs/Development/Task_Execution_Workflow.md)规定：功能分支提交推送后，workflow 固定候选执行 Mac mini 和 Windows 全量回归，审查及当前候选强制检查全部通过才允许合并；新候选不能沿用旧通过记录。独立 App 无宿主权限时仍遵循上述约束，并明确跨仓库证据缺口。
- App、Orca 和 SDK 是不同提交边界；检查实际工作区和目标提交，未经任务授权不升级依赖指针。
- 已授权的跨仓库提交/交付，子仓库目标提交推送成功后，及时同步本次任务涉及的父仓库 gitlink 并提交推送；开发分支引用同步不等待 CI 完成。CI 结果单独记录，正式合并/发布遵守对应验收要求。独立 App 无可访问宿主时说明该边界。
- 完整源码宿主、独立 App、Orca 单独构建使用各自的 CMake 入口。
- 公共类布局、虚函数或 ABI 变化需要匹配的 SDK；不能用 Release SDK 顶替 Debug SDK。
- 当前 UI 使用 SmoothUI/SmoothUIDock；组件 API、资源和主题 token 以可访问的当前源码或交付接口为准。
- 渲染、Journal、SDK 生产等任务需要按技能检查完整宿主或相应工具是否存在；独立 App 不依赖宿主私有源码或测试。

## 工作方法

各技能遵循同一决策顺序：明确任务目标与相关契约 → 查真实调用链和最接近的现有实现 → 优先复用已有接口、组件和工具 → 仅补当前能力缺口 → 验证结果并复查改动范围。步骤深度随任务调整，不强制每次新增接口、方案文档或审批。
扩展前说明现有能力缺少什么，新增状态、类型、配置、抽象或脚本由当前需求证明必要。每处改动应对应本次目标；必要的上下游配套一起完成，无关重构、风格统一和假设中的未来能力留在范围外。
现有实现可能包含缺陷，复用时核对契约与合理性，不盲目复制。共享实现的修改检查直接受影响的调用者，各层保持单一可信状态与规则来源。
先核对实际声明、消费点和调用链；旧说明与代码冲突时以当前声明和实现为依据，并说明差异。当前 `AutoRegisterDB::onCreated()` 已是 `final`，`TransactionGuard` 析构默认提交，不能套用旧初始化或自动回滚假设。
业务状态通过既有 DB、Handler/服务、Bridge 和通知链传播；QML 的弹窗与选择状态可以保持在界面层。不要把所有界面状态都写入 DB，也不要在 QML 重复实现内核参数规则。
按改动选择最小有效验证，保留与任务相关的取消、失败和重试路径。源码检查、CTest、SDK 探针、Journal 操作、截图与设备打印分别说明；只报告真正执行过的结果。
构建以当前 `CMakePresets.json` 和 CMake 实现为准；引用的构建目录、SDK 槽位、二进制与工具先核实。技能中列出的测试需先确认已配置、可用且与改动相关。
从完整宿主根目录开始的会话需显式读取本文件；App 子目录中的技能不会因位于仓库深处而自动成为宿主会话的技能清单。
