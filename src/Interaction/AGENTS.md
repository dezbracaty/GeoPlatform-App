# 应用交互与业务操作

涉及 Action、Handler、事务和注册时读取 `../../.agents/skills/gplatform-db-action/SKILL.md`。
涉及切片请求、预览发布或校准时读取 `../../.agents/skills/gplatform-slicing/SKILL.md`。
先核对实际 Action/Handler 入口、执行线程和生命周期，再选择修改位置。
先明确参数、结果、对象归属和事务语义，再复用现有 Handler、业务操作、对象图工具与注册机制；同一业务能力不按 UI/AI/Journal 入口分别复制实现。
区分 Exclusive、Background、Middleware、persistent 与显式异步任务；动作被接收不等于后台任务已完成。
文档数据捕获、后台计算和结果提交分别处理，失败路径不能假定 TransactionGuard 自动回滚。
