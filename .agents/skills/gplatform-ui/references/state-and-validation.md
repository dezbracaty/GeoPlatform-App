# 接线、国际化与验证

路径以 `APP_ROOT` 为准，宿主检查注明 `HOST_ROOT`。

## Bridge 与状态所有权

先查实际 `Q_PROPERTY`、`Q_INVOKABLE`、信号和注册宏，包含继承定义。应用中同时存在 `PropertyBridgeBase` 与 `bridge::ObservableBridgeBase`，不能假定所有 Bridge 使用同一套监听/事务模式。

- `src/Bridge/src/PropertyBridgeBase.cpp`：监听 ID，变更后重新读取 DB；`executeInTransaction()` 使用 TransactionGuard。
- `src/Bridge/src/ObservableBridgeBase.cpp`：QPointer 保护回调，按 ID 或全通知过滤。QPointer 不负责线程切换。
- `src/Bridge/src/DBPropertyBridge.cpp`：DB 列表和 Qt 属性投影；底层 DB 不是 Qt 属性依赖，需由通知触发实际更新。
- `src/Bridge/include/BridgeRegistration.hpp`：模块内 QML 注册宏。

新增属性同时核对读值、写入口、通知、初始状态、对象删除和界面重开。重连对象后移除旧监听，避免旧对象仍驱动当前界面。不要因某个基类注释称“线程安全”而直接从 worker 修改 QML 属性。
文档或模型持久状态放在相应 DB/业务层；弹窗展开、输入草稿、焦点等可放在 QML。只在确认提交时进入业务路径，保留项目既有即时编辑或事务交互。
复用现有信号和提交时机，不把 editingFinished/valueEdited 随意改为每次文本变化都写入业务，也不让 checked/value 的程序更新重复触发用户操作。已有 Bridge 能提供状态时优先绑定，避免增加一份需手工同步的镜像。

## 交互复用与改动范围

相似界面应尽量沿用同样的焦点、Tab/Enter/Esc、右键编辑、tooltip、滚动、弹层定位和关闭方式。先查组件已经处理哪些事件，再添加补充逻辑，避免外层 MouseArea/Shortcut 重复触发或抢走内部行为。
视口叠加面板需检查面板输入不会触发后方模型操作，可参考 PanelSurface；其他普通页面按实际父级判断，不机械套入拦截所有输入的透明层。
已有悬停延迟或动画是具体交互行为，按需要沿用；为修复状态传播而新增任意 Timer、轮询和强制重建则需要证据，不能把两者混为同一种机制。
沿用现有调用者的正常接线。新增跨层接口只覆盖本次需要，避免把布局任务扩展成 Action/Bridge 系统改造或与目标无关的全局风格统一。

## 异步任务与失效

以 `src/Interaction/src/CalibrationBridge.cpp`、`qml/components/CalibrationPanel.qml` 和 `src/Bridge/src/SlicingPreviewBridge.cpp` 为例，沿实际状态机检查：启动前校验、忙碌、进度、取消请求、后台确认、成功/失败、重新编辑及再次执行。
进度窗口关闭不能直接代表任务停止。配置、打印机、材料或源模型改变后，旧表单和产物可能失效；不要在仅关闭弹窗后恢复发送或导出。
校准表单字段及联动由 libslicer 提供，App 投影并翻译。不要在 UI 重写另一份默认值、范围、温度失焦或流量映射规则。

## 国际化

实现依据：`src/core/GettextBridge.cpp`、`src/core/AppInfo.cpp`、`CMakeLists.txt` 的 `i18n_update/gplatform_i18n_mo`，以及 `qml/components/SlicingSettingsContent.qml`。

- 应用源文案使用英文，QML 用 `qsTr()`，Qt C++ 用现有翻译入口。
- gettext PO 源文件位于 `resources/i18n/zh_CN/`，构建生成 MO 并嵌入资源；不添加英文 PO 或新 TS/QM 流程。
- libslicer schema 文案使用 `qsTranslate("LibSlicerSettings", text)` 或等价 Qt context；GettextBridge 将它路由至 `libslicer` domain，其他 context 走默认 domain。
- `i18n_update` 更新应用的 `gplatform.po`；不要认为它会自动生成所有动态 schema 翻译。按当前 libslicer 词库来源维护。
- 语言切换通过 AppInfo 加载词库和 `QQmlEngine::retranslate()`；检查动态文案与可扩展布局。

## 验证选择

小型外观调整检查实际布局、交互和相关主题；不为了覆盖每个样式值新增镜像测试。
复用或修改共享控件时检查与目标相关的 hover/pressed/disabled/focus、深浅主题、窗口缩放、长文案与直接使用处；只检查实际受影响项，不把每次 UI 修改变成完整组件库验收。
需要组件或接线检查时，完整宿主可查询 `HOST_ROOT/Core/tests/CMakeLists.txt` 中的 `AppUiThemeIntegrationCheck`、`ThemeBackgroundSyncCheck`、`PrintCommandBarLayoutCheck`、`FloatingPrintConfigurationCheck`、`CalibrationWorkflowCheck` 等目标。这些测试有各自范围，不能把其中一个通过扩大为全部页面已验证。
纯 Qt/offscreen 检查不能替代 Dock、焦点、原生窗口和真实渲染。用户要求真实程序验收时选择最小 Journal 场景，断言业务结果并保留截图用于外观核查。
独立 App 只运行当前源码和 SDK 能提供的检查，缺少宿主工具不生成假的通过结论。
