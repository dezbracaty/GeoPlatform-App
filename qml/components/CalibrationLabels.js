.pragma library

// Presentation and translation only. Field values, options, units, edit rules
// and validation are supplied by libslicer through CalibrationBridge.
function label(source) {
    return ({"Extruder type":"挤出机类型","Method":"方法","Calibration type":"校准类型",
        "Top surface pattern":"顶面图案","Material temperature range":"材料快捷范围",
        "Test model":"测试模型","Shaper type":"整形器类型","Print numbers":"打印数值",
        "Start PA":"起始 PA","End PA":"结束 PA","PA step":"PA 步长",
        "Start value":"起始值","End value":"结束值","Step":"步长",
        "Accelerations":"加速度列表","Speeds":"速度列表",
        "X start frequency":"X 轴起始频率","X end frequency":"X 轴结束频率",
        "Y start frequency":"Y 轴起始频率","Y end frequency":"Y 轴结束频率",
        "X fixed frequency":"X 轴固定频率","Y fixed frequency":"Y 轴固定频率",
        "Fixed damping":"固定阻尼","Start damping":"起始阻尼","End damping":"结束阻尼"})[source] || source
}
function choiceLabel(source) {
    return ({"Pass 1":"Pass 1（粗调）","Pass 2":"Pass 2（精调）",
        "YOLO (Recommended)":"YOLO（推荐）","YOLO (Perfectionist)":"YOLO（Perfectionist）",
        "Custom":"自定义"})[source] || source
}
function message(source) {
    if(!source)return ""
    const prefixes={"Invalid numeric calibration input: ":"请输入有效数值：",
        "Unknown calibration choice: ":"请选择有效选项：",
        "Expected comma-separated positive integers: ":"请使用整数与英文逗号填写："}
    for(const prefix in prefixes)
        if(source.indexOf(prefix)===0)return prefixes[prefix]+label(source.slice(prefix.length))
    return ({"Invalid pressure advance range":"压力提前范围无效，请核对起始值、结束值与步长。",
        "Acceleration values must be greater than speed values":"最小加速度必须大于最大速度。",
        "Temperature input must be unsigned integers":"温度请输入非负整数。",
        "Invalid temperature range":"温度范围无效，请核对起始值与结束值。",
        "Invalid volumetric speed range":"体积流量范围无效，请核对起始值、结束值与步长。",
        "Invalid VFA range":"VFA 范围无效，请核对起始值、结束值与步长。",
        "Invalid retraction range":"回抽范围无效，请核对起始值、结束值与步长。",
        "Invalid input shaping frequency range":"输入整形频率范围无效。",
        "Invalid input shaping frequency":"输入整形频率无效。",
        "Invalid damping factor":"固定阻尼无效。","Invalid damping range":"阻尼范围无效。",
        "Invalid cornering range":"转角范围无效，请核对起始值与结束值。",
        "High cornering values may cause layer shifts":"较高的转角值可能导致层移。",
        "Supported temperature range: 155-500 C":"支持温度范围为 155–500 ℃，已按原版规则处理输入。"})[source] || source
}
function title(mode) {
    return ({1:"压力提前",2:"压力提前",3:"压力提前",5:"流量比例",6:"温度",7:"最大体积流量",8:"VFA",9:"回抽",10:"输入整形 · 频率",11:"输入整形 · 阻尼",12:"转角",13:"公差测试模型"})[mode] || "校准"
}
function guidance(mode) {
    return ({1:"观察实物上挤出均匀的线，读取对应 PA 值。",2:"观察不同速度、加速度组合的转角，读取对应 PA 值。批量列表留空时使用原版默认。",3:"观察塔上转角与挤出变化，读取对应高度的 PA 值。",5:"观察实物顶面，选择线条连接均匀、表面平整的样块。",6:"从较高温度向较低温度测试；材料快捷范围只改测试温度，不切换材料预设。",7:"步长为原版每毫米塔高增加的体积流量系数。",8:"记录细纹明显或较平整的速度区间。",9:"观察回抽变化时的拉丝情况。",10:"观察实物 X/Y 方向振纹。设备参数的应用与保存由设备设置处理。",11:"在固定频率下观察阻尼变化。设备参数的应用与保存由设备设置处理。",12:"根据当前固件与 JD 配置显示转角单位和范围。",13:"载入 Orca 原版公差模型，无扫描参数；只记录配合观察。"})[mode] || ""
}
