# QML Bridge

修改 QML 接口时读取 `../../.agents/skills/gplatform-ui/SKILL.md`；涉及 DB 写入或业务操作时同时读取 `../../.agents/skills/gplatform-db-action/SKILL.md`。
核对当前类的基类、属性通知、对象归属和注册入口，避免绕过既有业务执行路径。
先查相近 Bridge 的正常接线与基类能力，通过现有属性/信号扩展；新增接口需有当前调用需求，不复制业务规则或另建状态同步层。
DocumentManager 的监听回调不自动切换 GUI 线程；QPointer 保护存活状态，也不提供线程调度。
新增 QML 接口检查声明、实现、通知及 Bridge 模块注册，保留已有对象删除和重新绑定行为。
