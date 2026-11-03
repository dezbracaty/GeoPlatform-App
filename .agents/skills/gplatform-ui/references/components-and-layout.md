# 组件、主题与布局

本文件路径以 `APP_ROOT` 为准；`HOST_ROOT` 是可选的完整源码宿主。修改前核对当前实现，下面是导航与决策依据，不是冻结的 API 清单。

## 找到实际组件

- 应用启动与主题同步：`qml/global/App.qml`。
- 主页面路由与校准 Loader：`qml/window/MainScreen.qml`。
- 打印工作区、prepare/preview/realistic 标签、环境同步：`qml/pages/3DPrintPage.qml`。
- 复用弹窗与表面：`qml/components/AppPopup.qml`、`qml/components/AppSurface.qml`。
- 工作区尺寸与主题映射：`qml/global/PrintWorkspaceStyle.qml`。
- 参数表单和异步任务 UI 示例：`qml/components/CalibrationPanel.qml`。

## 从相似页面选择复用入口

先从当前场景最接近的生产页面确认用途，再查候选控件的完整定义和基类；下表是导航，不是要求所有页面统一成同一种结构。

| 需求 | 当前复用入口与生产用法 | 应核对的行为 |
| --- | --- | --- |
| 通用弹层、标题与内容区 | `qml/components/AppPopup.qml`、`qml/components/PrintPopoverHeader.qml`；用例 `qml/dialogs/PrinterControlPopup.qml` | anchor/overlay、尺寸约束、展开、焦点、Esc 与关闭 |
| 面板表面与视口输入隔离 | `qml/components/AppSurface.qml`、`qml/components/PanelSurface.qml` | Theme/Frame 表面、层级、指针和滚轮是否穿透到 3D 视口 |
| 浮动设置内容 | `qml/components/FloatingSettingsPanel.qml` | 内容 Loader、lazyContent、可用区域、guideMode 与焦点；不要自建另一套相同浮层 |
| 属性分组、标签行和带单位数值 | `qml/components/InspectorSection.qml`、`qml/components/InspectorRow.qml`、`qml/components/InspectorNumberField.qml`；用例 `qml/components/SceneSettingsPanel.qml` | 现有列宽、草稿、editingFinished/valueEdited、范围和单位 |
| 普通数值参数与工具操作 | `qml/workspaces/normal/components/CutToolPanel.qml` 使用 NumberBox、IconButton、GroupBox、ScrollView | min/max/precision、提交时机、busy 与键盘/tooltip |
| 工具栏悬停展开 | `qml/components/HoverExpandableButton.qml`；用例 `qml/workspaces/normal/components/LeftPrintToolbar.qml` | 按钮/弹层 hover、延迟收起、禁用清理、提示和现有动作入口 |
| 应用设置与状态说明 | `qml/pages/SettingsPage.qml` | ScrollablePage、分组、主题选择、原有持久化与通知接口 |
| schema 驱动的参数表单 | `qml/components/SlicingSettingsContent.qml`、`qml/components/CalibrationPanel.qml` | 字段、单位、可见/可用状态、联动与库端校验 |

InspectorNumberField 与 NumberBox 有不同的提交和展示语义，按场景复用；不为了消除表面重复而在本次任务把所有数值输入强行合并。共享控件出现实际缺陷时在拥有职责的位置修正，不复制缺陷，也不顺带重构所有使用处。

完整宿主中，SmoothUI 源码位于 `HOST_ROOT/ThirdParty/SmoothUI`，Dock 位于 `HOST_ROOT/ThirdParty/SmoothUIDock`。修改这些依赖前读其适用指引。
独立 App 中先从选中 SDK 的 `qml/`、`qmldir`、`.qmltypes` 与随包 QML 查接口；SDK 路径从实际 `GPLATFORM_SDK_ROOT`、架构和配置确定。接口不全时明确缺少哪份定义，不按另一个 FluentUI fork 猜。

核对顺序：当前组件声明及基类 → 同类生产用法 → 文档。颜色查 `ColorResource.qml`，图标查 `FluentIcons.qml`，间距/尺寸查 `Theme.qml`。控件名相同不代表属性与 Qt 原生控件相同，例如当前 NumberBox 使用 `min/max/smallChange/precision`。

## SmoothUI 已承担的职责

以下路径相对上述实际 SmoothUI 源码根目录；独立 App 对照选中 SDK 的对应 QML 与 typeinfo。

- `Button.qml` 复用 `Controls/StandardButton.qml`：已有主题、Typography、padding、hover/pressed/disabled、图标和 FocusItem。优先配置它，不在页面重画同一套按钮状态。
- `TextField.qml` 复用 `Controls/TextBox.qml`：已有主题、文本选择、leading/trailing、右键/长按编辑菜单和焦点行为。自定义输入只补当前场景确需的部分。
- `Controls/NumberBox.qml`：处理范围、精度、增减、editingFinished 和相关输入；用真实字段与信号，不猜 Qt SpinBox 风格接口。
- `Frame.qml`：已有表面、边框、圆角、elevation 与阴影；AppSurface 在其上提供应用组合。自定义 background 会接管绘制，需明确哪些原有能力由新背景承担。
- `ScrollView.qml`、`ToolTip.qml` 和 `Controls/FocusItem.qml`：已有滚动条、弹层主题和焦点框行为，先检查可直接使用的能力。

注意目录与类型的实际归属：部分标准控件位于 SmoothUI 根模块，部分自定义控件位于 Controls。检查 imports 和同名类型的解析，必要时使用项目现有 alias 方式；不为本次需求大范围替换所有 QtQuick.Controls 导入。
依赖内的旧指引可能仍写 FluentUI 名称，最终以当前 QML 声明、模块定义和导入为准。

## 主题与尺寸

`AppPopup` 使用 `SmoothUI.theme: Theme.of(control)` 和 `SmoothUI.radius`；生产界面同时使用 `Theme.res`、强调色及 `PrintWorkspaceStyle` 的语义方法。沿组件继承与附加属性保持当前模式，不机械地给每个 Item 添加主题声明。
先复用 `Theme.space*`、控件高度、圆角、字体和工作区尺寸。新增常量判断是通用主题语义还是该界面专用尺寸，不为了消除所有数字而制造全局 token。
`qml/global/App.qml` 在主题初始化和有效深浅色变化时触发 `scene.theme.set`，将 2D UI 主题同步到场景。修改主题流程需同时检查 3D 背景。
同一交互层级优先复用现有字体、控件尺寸、间距、圆角、动画与 tooltip 习惯；不要只复制颜色却重新实现其余状态。业务颜色或确有需要的局部尺寸应表达用途，不为一个差异复制整套 Theme。

## 何时新增或扩展

先尝试直接使用、现有属性/slot 配置和小范围组合。确有缺口时判断职责：仅当前页面需要的组合保持局部；已有应用语义放应用组件；多个使用者共同需要的基础控件能力才考虑扩展 SmoothUI。
新增前能说明现有能力缺少什么、为什么组合不能清楚满足、影响哪些直接调用者即可，不要求每次新建方案文档。不要增加未使用的参数、预留模式、额外状态同步或另一层仅转发相同属性的壳。
公共组件修改检查现有实例的默认值、尺寸、主题与输入兼容性。独立 App 的 UI 由 SDK 交付，需修改库本身时确认源码和配套 SDK 构建边界，不能只改包中临时副本当作完成源码修复。

## Popup 与输入

`AppPopup` 的 `bodyData/footerData` 是内容入口；`preferredWidth/Height` 会受父区域限制。定位使用 `popupAnchor/popupVerticalAnchor`、`mapToItem` 和 `positionRevision`。布局异步完成后需要刷新定位时沿用 `reposition()`；不要每帧轮询目标位置。
检查实际 `closePolicy`、`closeOnEscape`、`windowEscapeEnabled` 和焦点范围。当前窗口级 Esc Shortcut 与 contentItem 的按键路径有不同职责，不能只改其中一个开关就声称所有 Esc 行为已改变。
保留可交互内容的焦点和鼠标事件处理；覆盖父级 MouseArea、滚动和弹层时检查事件是否被吞掉。忙碌操作的关闭可能表示取消请求，须跟踪后台终态。

## 页面、Dock 与布局

`App.qml` 在创建页面前调用 `SmoothUIDock.init()`。打印页面通过自身的页面激活函数切换 Dock 标签、激活 renderer，并按需要触发 `environment.switch`。增加页面入口需沿这条链路检查，不只改变一个 `visible`。
`prepare/preview/realistic` 是视图页面，业务环境字符串另有映射。真实效果页和准备页可能同属 normal 环境，不能把环境与页面做一一等同。
布局管理的子项使用 `Layout.*` 协商尺寸；锚点用于非布局管理的容器。核对直接父对象，不把 `anchors.fill` 与同一项的 Layout 尺寸约束混用。长文案、滚动、窄窗口和 DPI 都按项目实际目标检查，不套用通用固定字号重做整个界面。

新增 QML/单例需检查 `CMakeLists.txt` 中的 `QML_FILES`、单例列表、资源别名、打包后文件列表及 `qt_add_qml_module`。独立编辑器找到文件不能证明运行资源已包含它。
