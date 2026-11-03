# 应用请求、配置与结果发布

路径相对 `APP_ROOT`；`SLICER_ROOT=ThirdParty/OrcaSlicer`。

## 配置实际来源

`src/Slicing/src/SlicingBackend.cpp` 封装 Library 和配置操作；`src/Bridge/src/SliceSettingsBridge.cpp` 向 QML 投影设置；业务配置使用 `src/AppDB/include/SlicingConfigDB.hpp`。沿调用者确认修改的是当前配置、某个对象/打印床的配置，还是预设文件。
SDK 公开定义在 `SLICER_ROOT/include/libslicer/Library.hpp`、`SLICER_ROOT/include/libslicer/Config.hpp`，实现主要在 `SLICER_ROOT/src/libslicer_api/Library.cpp`、`SLICER_ROOT/src/libslicer_api/Config.cpp`。新参数需同时核对 PrintConfig 声明、SDK catalog/作用域/归属、合法序列化值、patch 消费点和 Print/PrintObject 的失效阶段。
耗材槽与物理工具不同，使用 `filament_physical_tools` 等显式映射；不能按材料名称或喷嘴个数推断连续纤维工具。
`SlicingBackend` 对长任务和配置修改有 admission/串行执行机制；不要为让 UI 即时更新而绕过其配置门控。

## 不可变输入与坐标

阅读 `src/Interaction/include/SliceSessionBuilder.hpp`、对应 cpp 和 `src/Slicing/include/SliceSession.hpp`：

- `capture()` 在文档线程读取 DB 标量与几何 ReadHandle、模型/表面色修订、配置快照和 masks。
- `build()` 做较重的 indexed mesh 转换，消费捕获值，不回读实时 DB；请求含 profile revision 与 source revisions。
- 实例矩阵按 row-major 4×4 传入，部件局部变换由构建阶段处理。核对每一步坐标，不把同一变换烘焙两次。
- `center_on_build_plate=false` 保留已捕获的场景放置；不要在 SDK 再次居中。
- `SliceObjectInput` 的 `model_path` 和 `volumes` 二选一。正常文档切片保留内存体积、角色、耗材槽、表面标记与 masks。
- mask 的 `mask_to_object` 是刚体仿射，尺寸承载缩放，父对象变换仍可能缩放/镜像。局部 override 只保存显式覆盖，不能填满全局默认值后误覆盖继承。

SDK `Library::slice_impl()` 从快照重建并归一化配置，校验材料绑定、输入/有效 mask，建立私有 Model 后调用 Print。检查最终有效配置，不能只看 QML 文本或最初 patch。

## 预览与产物

阅读 `src/Interaction/src/ToolpathPreviewPreparation.cpp`、`ToolpathResultPublication.cpp`、`src/Bridge/src/SlicingPreviewBridge.cpp` 与 `src/AppDB/include/SliceOutputRecord.hpp`、`src/AppDB/src/SliceOutputRecord.cpp`。
预览准备和输出记录是不同阶段；发布以 epoch、产物可读性、DB 注册/归属和事务保护。旧预览与材质在失败/回滚通知排空前保活；commit 后才安装已准备的输出。
取消或替换的 epoch 不得发布。是否允许“G-code 可用但预览失败”取决于 source scope 和当前实现；导入 G-code 没有兼容预览时不按普通切片降级。不要自己扩大降级条件。
预览是打印输出的消费者，保持 SDK schema、原生圆弧、沉积类别与 `source_command_id`。不能把显示简化折线当作实际打印几何。

## 校准

新增或提取此类能力时先按 [原始能力提取与协议封装](original-code-and-protocol.md) 定义库契约并查阅原始 GUI 调用链。
`src/Interaction/src/CalibrationBridge.cpp` 与 `SLICER_ROOT/src/libslicer_api/OrcaCalibrationApi.cpp`、`Library::prepare_calibration/slice_calibration` 是实际入口。表单默认值、联动、预检和校准模型由库处理；App 负责展示、翻译、进度和产物接线。
保留原生校准任务的模型/配置；普通 `SlicingHandler` 入口不是替换校准流程的捷径。

## 对应验证

Orca SDK 用例查询 `SLICER_ROOT/tests/libslicer_api/test_config_api.cpp`、`SLICER_ROOT/tests/libslicer_api/test_orca_plater.cpp` 的实际标签，例如 `[fiber-mask]`、`[resin-fill]`、`[fiber-disabled]`、`[calibration-parameters]`。
完整宿主可查 `Core/tests/CMakeLists.txt` 中校准、Toolpath 生命周期/发布、预览范围和退出检查。CTest 测试名与可执行目标名可能不同，例如 Toolpath 生命周期多项测试共用 `ToolpathLifecycleCheck`。
最后核对实际应用加载的 SDK 与资源来源，再判定 UI 参数是否真正到达算法。
