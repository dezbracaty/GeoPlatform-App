# Action、Handler 与注册

路径相对 `APP_ROOT`。

## 执行与完成

入口是 `src/Base/BaseInteraction/src/ActionManager.cpp` 的 `triggerActionInternal()`，配套 `ActionContext.hpp`、`IActionHandlerBase.hpp` 和 `ActionHandlerRegistry`。先检查 admission、当前 environment、HandlerType、参数和既有 active handler。
`src/Interaction/include/StandardActionHandler.hpp` 默认是 Exclusive；Background、Middleware、persistent 和显式异步 Handler 的调度及清理路径不同。不能只把任务丢进 worker 后依赖基类默认清理。
结果包含 `accepted/success/pending/result/error/error_code` 等字段。对异步调用追踪 completion 或 invocation 结果，不能将一次布尔返回或 actionCompleted 信号不加分析地作为全部后台工作完成的证据。
标准 Esc 仅对活跃 Exclusive 工具默认请求退出，后台/中间件不应因此吞掉 Esc。修改取消行为需核对工具自身 onExit、任务取消、资源释放和重新进入。

## 正常入口与 AI 契约

普通 UI 可通过 `ActionManager.triggerAction(code, params)` 或已存在的 Bridge/服务接口执行。新增能力复用最接近的生产入口。
先列清本次所需参数、选择来源、输出、错误和完成时机，再检查已有 action 是否可直接调用或以有限参数扩展。不要为同一业务行为按菜单、快捷键、AI 或 Journal 分别复制执行算法；确需不同交互准备时让入口复用底层业务操作，而不强行合并无关状态机。
用户要求 AI 可调用时，再检查 `getAIDescriptor()`、`AIDescriptorHelper.hpp`、`onEnterForAI()` 及参数 schema。写操作 descriptor 需要结构化契约；声明的动作必须由真实 Handler 实现，并设置正确的 error/result。
不要为了 UI 功能默认开放所有动作给 AI，也不要将 descriptor 元数据当作实际权限或实现。
`UndoRedoHandler.cpp` 展示普通入口与 AI 入口范围可能不同，验证时分别说明。

## 模块注册

- `src/Interaction/include/InteractionRegistration.hpp`：`REGISTER_INTERACTION_ACTION`、persistent 与 QML singleton 宏。
- `src/Bridge/include/BridgeRegistration.hpp`：Bridge 的 QML type 和 custom singleton 注册宏。
- 模块的 `*Registration.cpp`：拥有自己的 ModuleRegistrationList 和 `registerModule()`。
- `cmake/ApplicationModuleComposition.cmake`：消费全局模块列表，生成 Application 调用文件。
- `src/Application/src/main.cpp`：先执行模块注册，再加载正式 QML。

新增类沿所属模块的注册机制登记；检查该翻译单元进入构建及静态链接保留。模块不反向包含 Application 注册头，不在 main.cpp 再维护一份具体类清单。
若新增完整模块，再核对 Foundation 提供的 `gplatform_register_module()` 和现有 CMake 用法；独立 App 中 Foundation 来自 SDK/应用组合。
快捷键要在动作注册完成后解析，新增菜单、快捷键或启动动作需核对当前注册/启动顺序。
已经存在的模块列表、注册宏和启动组合能够承载需求时直接使用，不另设手工类清单、第二个注册容器或隐藏启动扫描。新增 API 或注册项需有真实调用者，避免预留未使用的入口。

## 验证

根据修改选择启动 admission、模块注册、键盘快捷键、environment 路由和 Handler 生命周期检查。独立 App 的 `StartupAdmissionCheck` 在 `CMakeLists.txt` 中由 `GPLATFORM_BUILD_SDK_CHECKS` 启用；更多检查只在完整宿主可用。
若用户要求菜单、快捷键或拖拽，验收必须包含对应输入；直接调用 Handler 只能验证业务执行部分。
