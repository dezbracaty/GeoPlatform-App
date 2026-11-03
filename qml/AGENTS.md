# 应用 QML

修改界面前读取 `../.agents/skills/gplatform-ui/SKILL.md`，核对相邻生产页面、组件和 Bridge 接口。
先看相似生产页面与已有应用封装，再核对当前 SmoothUI/SmoothUIDock 组件；优先通过已有属性、内容槽和信号复用控件、主题与交互，不根据其他 FluentUI 项目推断 API。
已有能力满足需求时直接使用；确有缺口才做必要组合、扩展或新增，不复制整份控件、提前搭通用框架或顺手重构无关页面。
主题、布局、业务接线和验证的具体规则由技能及其参考维护。
修改真实交互时核对控件 `objectName`、焦点与事件路径；涉及参数表单时沿现有 schema/Bridge，不在 QML 复制内核规则。
新增 QML 文件或单例核对 App `CMakeLists.txt` 的打包清单和资源路径。
