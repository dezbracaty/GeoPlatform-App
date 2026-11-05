# 场景验收：DB 或标准截图

## 两种方式

| 类型 | 场景目标 | 比较依据 |
| --- | --- | --- |
| `db` | 导入、编辑、配置、涂色、撤销重做等数据结果 | 正式 `verify.db` 比较实际 DB 序列化与标准 DB |
| `screenshot` | 面板、弹窗、布局、图层显示和渲染外观 | 正式 `verify.screenshot` 比较实际画面与标准截图 |

每个场景选择一种验收方式。数据场景中的截图用于诊断，不自动增加像素门禁。
一个场景同时承诺独立的数据和外观目标时，拆清验收目标；不增加新的验证方式。
原生 CTest 和既有 G-code 文件专项检查保留各自证据，不代替 DB 或标准截图验收。

## 场景定义

metadata v3 的 `verification.requiredMethods` 只允许单项 `db` 或 `screenshot`。
`acceptanceTarget` 写明具体检查对象和目标，`Journal.json` 保留正式输入及检查点。
每个场景保存自己的标准数据，共用 Journal 的获取和比较代码，不配置脚本路径或自定义命令。

DB 检查点沿用 `verify.db`，格式为 `transdb.cereal-portable-binary.v1`。
当前接口按实际 DBInstanceID 获取单个对象的完整属性存储，不代表整个文档关系图。
标准 DB 保存在资产仓库 `baselines/db/<case>/`；`dbBaselines` 将文件绑定到 Journal 内的 expectedSerialized。
入口校验两者相同，并记录文件哈希。对象 ID、初始化配置及关联对象必须随场景说明，禁止把未识别对象当目标。

截图检查点沿用 `verify.screenshot`，引用资产仓库 `baselines/screenshots/<case>/` 中的标准图片。
场景固定窗口、DPR、主题、语言与适用平台；比较容差写在正式检查点。
截图必须检查实际画面，不能用黑图、其他窗口或错误输出作为标准。
`verify.screenshot` 可用 `region: [x, y, width, height]` 指定一个物理像素矩形；省略时仍比较整窗。
矩形必须在同尺寸的两张原图内，非法或越界不能通过。报告保存完整 expected/actual，diff 只标记该区域的差异，并写明比较范围。
区域按验收目标预先声明：例如引导卡内容和选中标签分别比较，动态 FPS、非本场景目标的时间抖动渲染不作为 UI 引导的像素标准；不降低目标区域的精度，也不声称覆盖区域外外观。

## 本轮建立标准的过程

先执行场景并采集候选 DB/截图，按明确业务要求核对，再保存标准文件及来源、哈希和审阅依据。
随后在新的 App 进程中重放，使用已有比较器验证；同时用有意错误的 DB/图片确认比较能拒绝错误结果。
没有标准时继续补标准，不把“没有标准”当作验收完成，也不把候选输出自动批准。

`capture.db`、`screenshot.capture` 是采集动作，不能单独产生通过结论。
`--list` 同时列出资产状态、验收类型与缺少的检查点；当前结果以实际运行的 case_result.json 为准。
历史六类方法的 v2 报告保留原文，不能解释为 v3 的 DB/截图通过。

## 结果

执行、清理和该场景的全部正式比较通过才判 passed；实际不符判 failed。
缺少检查的诊断回放仍为 not_verified，错误输入或缺文件在启动前明确报错。
DB、图片的 expected/actual/diff 和原始 Journal 报告保存在统一运行目录。
P3 的原有文件检查标为 supplementalResults，不能让缺少 DB 比较的场景变成 passed。

场景逐项分类、目标与现有检查点见资产仓库 `docs/scenario-acceptance.md`。
