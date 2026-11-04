# 校准流程与证据

用于用户要求校准真实流程验收时。下列场景和工具路径相对完整宿主 `HOST_ROOT`；原生库参考路径为 `APP_ROOT/ThirdParty/OrcaSlicer`。先从当前菜单、参数 schema 和实现确认范围，再复用现有场景，不把固定场景数量当作永久的“全部校准”。

## 复用已有场景

场景入口均为 `Core/tests/journals/<目录>/events/Journal.json`，同目录的 `metadata.json` 说明前置配置、输入和独立预期。以实际代码核对 metadata，勿把历史执行结果当作本次通过依据。

| 校准分支 | 场景目录 |
| --- | --- |
| PA Line / Tower / Pattern | `calibration_pa_line`、`calibration_pa_tower`、`calibration_pa_pattern` |
| 普通流量粗调 → 精调 | `calibration_flow_coarse_refine` |
| YOLO → Perfectionist | `calibration_flow_yolo_refine` |
| 第二材料槽及图案保持 | `calibration_flow_second_material` |
| 温度 / 最大体积流量 | `calibration_temperature`、`calibration_volumetric` |
| 回抽 / 转角 | `calibration_retraction`、`calibration_cornering` |
| 输入整形频率 / 阻尼 | `calibration_input_frequency`、`calibration_input_damping` |
| VFA / 公差 | `calibration_vfa`、`calibration_tolerance` |

现有夹具以默认 AD5X/0.4、Flashforge Generic PLA、Klipper 和默认窗口布局为前提。改变机器、材料、映射、固件或布局时重新核对选项、范围、输出指令和结果作用位置，不能直接沿用旧坐标与预期。

## 业务链与独立检查

1. 经真实菜单打开目标项，核对标题、参数、材料槽和图案。输入整形包含子菜单；点击未进入表单时沿真实菜单到 Action 的链路定位，不改成直接调用 Bridge 来绕过问题。
2. 输入受控参数，生成模型并切片，检查预览终态和对应校准模型。按场景记录异步预算，未生成产物时不能把截图动作成功当作切片完成。
3. 经现有文件选择器导出 G-code 和 Gcode.3MF。检查文件确实由本次运行生成、非空、含有效挤出及模式对应扫描；按盘定位包内 G-code，与对应单独导出逐字节比较。包与单独导出一致只证明导出一致性，不能代替原始算法对照。
4. 经结果界面填写或选择受控观察，核对应用生成的原始记录：任务/模式、参数、目标槽、观察值、是否应用及应用前后 revision。需要软件模拟观察时在备注中明确标记，不把勾选确认描述为已完成实物打印。
5. 对采纳类检查实际目标值更新、其他材料保持不变、旧输出失效且未自动保存材料预设；对仅记录类检查未自动修改切片配置或设备设置。流量继续精调时检查新基准、目标槽、图案与轮次；第二材料场景还需核对第一材料。

流量独立数值例：普通法以0.98选标记5，候选为1.029；精调以新基准选-1，候选为1.01871。YOLO以0.98选0.01得到0.99，Perfectionist以0.99选0.005得到0.995。实际候选来源仍是任务样块接口，验收侧独立核对，不能在 App 重写换算规则。

当前控件定位可查 `CalibrationPanel.qml`、SmoothUI `MenuItem.qml` 和 `Controls/FileBrowserDialog.qml` 的真实 `objectName`；文件选择器属于独立窗口。先确认实际加载的组件/SDK版本，再使用具名目标，不向控件注入业务状态。

## 原始 Orca 对照与失败定位

UI 流程与原始算法一致性分别验证。已有入口见 Orca `README.md` 的“验证入口”：`orca_plater_tests` 使用正式 SDK 检查几何、配置、扫描指令和预览；`orca_plater_reference_tests` 以相同生产 API 对象文件构成的独立测试动态库保留完整逐条 G-code 对照；`OrcaPlaterSource` 检查原始来源。

完整指令不同时，固定模型、有效配置、运行库与目标层，定位首个差异；还需用相同原始模型/配置重打，检查参考本身是否可重复。严格对照的并发约束必须在两侧各自的 TBB 运行时内生效，仅调用方的 `global_control` 可能约束不到 SDK 私有静态 TBB。现有参考夹具同时使用两侧的单线程 `global_control` 和 `task_arena`，不要通过生产库强制串行、排序最终 G-code 或放宽完整相等断言消除差异。

## 留证与清理

使用正式 Runner 创建新运行目录，保留 JSON 哈希、执行结果和报告；将独立检查的导出文件、原始观察记录及差异证据归档到对应运行目录。核对截图是否被权限窗口、屏保或其他弹窗遮挡，必要时补跑，不能据被遮挡图片判断界面正确。

仅清理本次明确拥有、已归档并核对一致的测试文件和观察记录，避免污染用户校准历史。长期保留声明式场景、受控资产和使用规范；临时调查日志、逐轮记录及运行产物留在忽略的构建目录。最终分别报告正式 acceptance、独立检查结论、原生对照结果、实际覆盖和未验证范围。
