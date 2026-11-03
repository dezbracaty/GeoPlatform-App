# 渲染源码、帧与拾取契约

本技能在 App 内维护，路径显式区分 `APP_ROOT` 与可选 `HOST_ROOT`。只有 App/SDK 时可核查公开接口、DB/交互输入及运行结果；私有实现修改需要对应源码。

## 入口与所有权

App 接口：`src/Base/BaseRender/include/IRendererSession.hpp`、`IRendererBackendFactory.hpp`、`IRenderPickCapability.hpp`、`PresentableFrame.hpp`、`FrameSynchronization.hpp`、`GraphicsContextHandle.hpp`、`RendererInstanceContext.hpp`。
App 具体渲染：`src/Rendering/Vtk/`、`src/Rendering/Filament/` 和 `src/Rendering/Common/`。通过 SDK 的 `Core/RendererBackends/{Vtk,Filament}/include/` 声明继承基类、访问原生资源并注册工厂。
App 交互：`src/Interaction/src/PickService.cpp`、`ViewportInputRouter.cpp` 及对应头文件；DB read view 在 `src/Document/`。

宿主实现：

- `Core/src/Render/src/RenderingBootstrap.cpp`：设置实际图形 API，并注册已编译后端。
- `Core/src/Render/src/threadRenderQmlItem.cpp`：视图绑定、Session 创建与 Qt Scene Graph/窗口生命周期。
- `Core/RendererBackends/Vtk/src/VtkRendererSession.cpp` 及 `Core/RendererBackends/Vtk/src/private/DBRenderAdapter.cpp`：VTK Session、文档订阅与更新队列。
- `Core/RendererBackends/Filament/src/FilamentRendererSession.cpp` 与 private 目录中的 SceneAdapter：Filament 运行与资源管理；具体 DBSync 位于 App。
- 两个后端各自的 README、CMake 与宿主 `Core/cmake/RendererBackendSDK.cmake`：组合与私有类型边界。

新 DB 的业务注册不自动赋予后端绘制支持。核对当前角色、工厂中的类型注册、DBSync 和该后端实际支持的能力。
界面和交互不能引入私有 RenderThread 或原生渲染类型绕过 Session/能力接口；App 的 Rendering 模块可使用公开的原生扩展头，不能访问私有线程实现。

## 复用次序与新增边界

先看相同 role/view 的现有用法与实际后端，再选择修改位置：DB/关系输入 → 现有同步/外观工具 → 后端 DBSync/native 资源 → Session/帧协议 → Qt 呈现。上游状态错误不靠后端特例修补，单个材质或几何同步问题不默认改变公共帧接口。
`APP_ROOT/src/Base/BaseRender/include/RendererBackendCatalog.hpp` 和 `IRendererBackendFactory.hpp` 已提供后端查找与 Session 创建；沿当前注册/组合方式接入，不加第二套后端管理器。
App `src/Rendering/Common/ModelRenderAppearanceResolver.cpp` 处理共享模型外观语义；涉及耗材色/源外观时先查已有 resolver 与两个后端调用，不在 App、VTK、Filament 各维护一份规则。后端特有实现保持私有，不为了复用一小段代码扩大公共 SDK 面积。
新增公共字段或能力前明确输入/输出、线程、坐标、资源归属及不支持时的结果，检查现有消费者能否继续遵守契约。只修改当前需求影响的路径；不能通过无条件全量 rebuild、复制场景或额外线程掩盖原有失效/调度错误。

## 线程与文档更新

`DocumentManager` 发布回调不保证 GUI 线程。VTK adapter 的 subscribe 回调只 scheduleUpdate，renderer-owned DBSync map 在渲染线程处理。
检查创建/删除/属性更新合并、完整同步、对象保活、文档解绑和 shutdown；不能只在回调给渲染对象加锁就认定生命周期正确。
Filament 使用自己的 adapter 与 native 资源管理，不把 VTK 队列和对象假设直接套过去。

## 帧交换

`IRendererSession::frameReady()` 不携带权威帧；Scene Graph 在呈现时调用 `tryAcquireReadyFrame()`，应只取可立即采样的帧。
Session 创建 info 带 API、设备身份、Transferred context 和 surface owner。工厂成功消费 context，失败则调用方保留释放责任。
区分 acquire 后的安装确认 `acknowledgeFrameAcquired()`、未采用帧的 `discardFrame()` 和已使用帧的 `releaseFrame(FrameRelease)`。需要 consumer completion 的后端必须等 GPU 真正结束采样才复用纹理；安装确认本身不是释放。
VTK 的 `Core/RendererBackends/Vtk/src/private/VtkFrameExchange.hpp` 显式维护 Free/Rendering/AwaitProducer/Published/Acquired/Retired、generation 和 consumer completion。旧 FrameId 不得释放新复用纹理；resize 和暂停不能破坏仍被使用的帧。
Filament Session 有自己的 ready queue、frame-to-texture 映射和 release 逻辑，分别核查，不假定相同缓冲数量或回收策略。
deferred shutdown 先停止生产，再允许 presenter lease 和 GPU completion 排空；不要让视图关闭强行销毁仍被采样的资源。

## 拾取与坐标

`PickService::attachRenderView()` 记录 view ID、role、generation、logicalSize 和 pickCapability。重新绑定会取消旧请求/缓存；完成结果须匹配当前代次。
从窗口/QML 逻辑坐标到 normalized viewport、物理渲染尺寸、世界射线、实例与部件局部坐标逐步核对；检查 DPR、边界点、视图切换与销毁后的完成回调。
App 消费能力接口，不按当前默认后端假定所有 detail/features 都支持。

## 检查选择

宿主/后端 CMake 中查询真实目标：`VtkFrameExchangeCheck`、`RendererViewContractCheck`、`VtkPickIntegrationCheck`、`OpenGLFrameCompletionCheck`、Filament 后端契约/场景/集成检查。
App 的 `PrimitivePickCheck` 与公开扩展检查只覆盖各自接口。真实应用可选择 renderer tab lifecycle、realistic transition、preview camera 等已有 Journal 场景。
按修改检查多视图、快速 resize、标签切换、暂停、关闭和重开。GPU 集成测试需要相应环境，offscreen 结果应明确范围。
