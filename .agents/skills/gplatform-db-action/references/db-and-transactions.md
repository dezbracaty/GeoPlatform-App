# DB、关系与事务

路径相对 `APP_ROOT`；TransDB 内部实现位于存在时的 `HOST_ROOT/ThirdParty/TransDB/rttr_version`，独立 App 使用 SDK 公开接口。

## 先查同类操作与可复用工具

先确定操作涉及的对象层级、输入、输出和历史语义，再选择现有能力：

| 需求 | 当前入口 | 复用时核对 |
| --- | --- | --- |
| 对象图复制/粘贴 | `src/Document/include/DBCopyService.hpp`、`src/Interaction/src/CopyPasteHandler.cpp` | 根对象、ID 映射、共享/归属关系、失败回滚 |
| 模型实例与对象图操作 | `src/AppDB/include/ModelGraphUtil.hpp`、当前调用它的 Handler | 实例与共享对象的边界、关系和清理 |
| 对齐变换计算 | `src/Interaction/include/TransformCalculator.hpp` 与对应对齐 Handler | bounds、坐标、床面和现有 Transform |
| 网格射线查询 | `src/Base/BaseGeometry/include/MeshRayQuery.hpp` 与现有拾取链 | 局部/世界坐标、变换和命中语义 |
| 属性、归属和依赖传播 | `src/Document/include/DBRelationRegistry.hpp`、`src/DBRelations/` | 已有字段、关系名称、删除策略与通知 |

这些工具只在其实际职责匹配时复用。已有能力不满足时补拥有该职责的实现，不因名称相近强行复用，也不在新 Handler 手工重写一套对象图/变换/归属算法。
单个业务参数不自动需要新增 DB；只有它确实属于持久或共享业务状态时才扩展相应对象。派生显示值和临时编辑状态无需为了“统一”加入序列化和事务历史。

## 对象创建与初始化

阅读 `src/Base/BaseDB/include/AutoRegisterDB.hpp` 与 `src/Base/BaseDB/src/AutoRegisterDB.cpp`。当前 `onCreated()` 为 `final`，按以下阶段执行：初始化钩子在 DerivedUpdateGuard 下运行 → 注册到 DocumentManager → 记录生命周期创建 → `onFullyInitialized()`。
使用现有工厂路径创建对象，保证 shared_ptr 建立后才执行注册。扩展 `initializeProperties()`、`afterPropertiesInitialized()` 或相应派生钩子；不要照旧注释覆盖 `onCreated()`，也不要在构造函数调用依赖 shared_from_this 的注册。
`DerivedUpdateGuard` 用于派生/初始化更新的历史语义，不能为绕过用户编辑历史而随意包住业务写入。

## 模型对象图

从以下真实类型与实现确认对象归属：

- `src/AppDB/include/ModelInstanceDB.hpp`：实例变换、对象引用、打印床和切片配置。
- `src/AppDB/include/ModelObjectDB.hpp`、`ModelPartDB.hpp`、`ModelGeometryDB.hpp`：共享对象、部件局部变换、几何与读句柄。
- `src/DBRelations/ModelGraphRelations.cpp`：object/part/instance/geometry/material/surface-color 的关系、级联删除和变更传播。
- `src/Document/src/DocumentManager.cpp`：注册、移除、归属与通知的实际行为。

操作实例不能无意修改所有共享实例的几何。复制、删除、拆分、换材料和表面标签修改需要核对所有权与共享引用；不能只复制顶层 Actor 指针。修改共享几何时使用当前几何读写与 revision 协议，不能直接改只读快照的 VTK 数据。
关系变化应保留相应通知链。当前实现会把几何、材料、表面色变更传播到 part/object/instance；UI 或 DBSync 只接收了顶层通知不代表下游数据已经正确重建。

## 事务行为

从实际操作确认事务边界，例如 `src/Interaction/src/MirrorHandler.cpp` 的 `executeMirror()` 与 `src/Interaction/src/UndoRedoHandler.cpp`。
TransactionGuard 的实现位于宿主 TransDB 的 `TransactionManager.cpp`：构造 begin，未显式结束的析构默认 commit。需要撤销部分修改时显式 rollback；对捕获异常后继续返回成功的旧模式先分析，不作为新功能模板。
批量编辑保留合适的用户历史粒度；撤销需要同时恢复属性、对象生命周期、归属和派生消费者。不要把手工发通知当作实际属性或关系变化的替代。
`DocumentManager::notifyListeners()` 复制监听列表后在调用线程执行；事务完成会排空 pending changes。处理线程、通知批次与对象保活，避免异步访问已删除 DB 或在回滚通知排空前销毁必要对象。

## 临时对象

`src/Document/include/TempDBScope.hpp` 使用正常具体 DB 类型和 DocumentManager，通过负 ID 与受控注册生命周期区分临时对象。沿 `createTempTracked()` 和 scope 清理路径检查，不另外构造一套临时文档或用业务 Undo 代替临时清理。

## 检查入口

完整宿主 `Core/tests/CMakeLists.txt` 中按需选择 `ModelGraphOwnershipContractCheck`、`ModelGeometryVersionContractCheck`、`TempDBContractCheck`、`AppDBSerializationRegistrationCheck`、关系检查或 `ModelEditingAcceptanceCheck_*`。
验证用户要求的实例/部件和未修改对象、失败后状态、Undo/Redo 与重新操作；静态关系检查不能替代真实输入验证。
公共复制、关系或生命周期代码变更时，选择直接依赖该语义的代表调用者验证；不为减少 diff 把修复放进某个下游特例，也不无关地重构所有 Handler。
