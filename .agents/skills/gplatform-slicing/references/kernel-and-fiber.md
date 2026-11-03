# 内核阶段、连续纤维与验证

本文件路径以 `SLICER_ROOT` 为准；修改前读取该库自己的 `AGENTS.md`。

## 找到拥有错误数据的阶段

| 问题 | 优先核查 |
| --- | --- |
| 输入对象、材料、有效配置 | `src/libslicer_api/Library.cpp`、`Config.cpp`、`include/libslicer/Library.hpp` |
| 调度、失效与 G-code 入口 | `src/libslic3r/Print.cpp`、`PrintObject.cpp`、`GCode.cpp` |
| 切层/墙/填充/支撑 | 对应 `PrintObject` 阶段、`PerimeterGenerator`、`Fill/`、`Support/`，先搜索当前消费点 |
| 纤维候选、执行和覆盖 | `src/libslic3r/ContinuousFiber/` 及其调用方 |
| 参数与预设归属 | `PrintConfig.cpp/.hpp`、Preset 相关实现、SDK catalog |

从错误的截面、Surface、余域、填充实体、最终执行计划或 writer 输出反向追踪；不要在无关阶段追加补丁。涉及新参数时同时检查 `Print::invalidate_state_by_config_options()`、PrintObject 的失效逻辑与实际消费处。

## 单位与几何

`src/libslic3r/libslic3r.h` 根据构建选择 coord_t，`SCALING_FACTOR` 可变；用现有缩放函数，不硬编码 1e6。明确 mm、内部坐标、面积比例平方、mm/s 与 G-code F 的 mm/min。
区分 slice_z/print_z、线宽/线距/体积流量、对象局部/实例/机器坐标与最终量化。覆盖统计按相应布尔并集而非简单相加，保留外轮廓、孔洞、连通性及合法退化结果。

## 连续纤维的关键边界

直接阅读 `src/libslic3r/ContinuousFiber/PreparedFiberPath.hpp`、同目录的 `FiberCoverageLedger.hpp/.cpp`、`FiberPathFinalizer.hpp` 及当前 Print 中的编译、认证与缓存逻辑。

- 路径包含来源区间、执行区间、落纤和运动语义，裁剪/接缝不能反转来源或连接本不连续的片段。
- `FiberMotionKind` 区分主动供料、CUT 后被动沉积、非沉积 finish。供料量、沉积覆盖和运动长度不能视为同一量。
- `PreparedFiberPath` 与 FrozenFiberExecution 经过最终化/认证后供下游使用。修改编译、机器绑定或量化时保留发布的几何/材料契约，不能再用通用反转、简化和重新圆角随意改写。
- `FiberCoverageRecord` 记录 accepted contour/infill exclusion、resin domain、intentional void 和 execution envelope。纤维拒绝后需正确回补树脂；不能只删掉 fiber 折线却保留已扣减的区域。
- G/U 分区由受封存路径和分配域派生，详细统计可选；不要伪造未计算的诊断数值或以扩大容差掩盖未分配面积。
- Print/PrintObject 存在几何与执行计划失效、每实例执行绑定和缓存代次。检查 fresh/cached、开关切换、取消/失败后重试及普通打印隔离。

保留 TBB 并行、取消检查和共享数据不可变约定；不能通过共享层指针或对象局部缓存修改其他实例。

## 构建与验证

宿主/独立 App 的 libslicer 来自 `APP_ROOT/cmake/thirdparty/fetch_libslicer.cmake`：缓存按源码绝对路径、架构和 ABI 隔离，在 configure 时构建安装。先核对 CMakeCache 的 `GPLATFORM_LIBSLICER_CACHE_ROOT/INSTALL_DIR/PACKAGE_DIR` 与实际部署库。
Orca 单独构建使用该库 presets，启用 `LIBSLICER_BUILD_TESTS` 并选择相关测试目标/标签；不要默认运行完整长模型矩阵。
核查 `tests/libslic3r/test_continuous_fiber.cpp` 和 `tests/libslicer_api/test_config_api.cpp` 中与阶段相符的用例。普通模式回归可用 `[fiber-disabled]`；余域/mask/校准按实际标签选择。
完整宿主另有 `Core/tests/slicing_regression` 和相关脚本，基线有来源与输入冻结规则，不能为了通过覆盖已有标准。
几何修改检查最终占料与树脂余域；速度优化同时报告同条件耗时与结果等价；UI 截图和成功生成 G-code 文件不足以证明这些不变量。
