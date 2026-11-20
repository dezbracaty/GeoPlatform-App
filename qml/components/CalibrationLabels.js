.pragma library

// Presentation and translation only. Field values, options, units, edit rules
// and validation are supplied by libslicer through CalibrationBridge.
function label(source) {
    return ({"Fiber landing parameter":"扫描参数","Fiber landing height":"落点高度",
        "Fiber landing length":"落点长度","Fiber landing speed":"落点速度","Fiber extra prefeed":"额外出丝量",
        "Fiber trial spacing":"试线间距",
        "Base material":"基材","Fiber material":"纤维丝",
        "Calibration material":"校准材料","Calibration flow baseline":"本轮流量基准",
        "Extruder type":"挤出机类型","Method":"方法","Calibration type":"校准类型",
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
        "Fiber landing height":"落点高度","Fiber landing length":"落点长度",
        "Fiber landing speed":"落点速度","Fiber extra prefeed":"额外出丝量",
        "YOLO (Recommended)":"YOLO（推荐）","YOLO (Perfectionist)":"YOLO（Perfectionist）",
        "Custom":"自定义"})[source] || source
}
function message(source) {
    if(!source)return ""
    const spacingPrefix="Minimum fiber trial spacing: "
    if(source.indexOf(spacingPrefix)===0)
        return "试线中心距至少需要 "+source.slice(spacingPrefix.length)+"，请给凹字和纤维留出间隔。"
    const prefixes={"Invalid numeric calibration input: ":"请输入有效数值：",
        "Unknown calibration choice: ":"请选择有效选项：",
        "Expected comma-separated positive integers: ":"请使用整数与英文逗号填写："}
    for(const prefix in prefixes)
        if(source.indexOf(prefix)===0)return prefixes[prefix]+label(source.slice(prefix.length))
    return ({"Fiber heater waiting is not verified for this machine":"此机型的纤维加热器等待能力尚未验证。",
        "Select a thermoplastic base material and a continuous fiber material":"请选择热塑性基材和连续纤维材料。",
        "Fiber calibration requires a profile that can switch to fiber without a prime tower":"请使用支持无擦嘴塔切换纤维工具的配置。",
        "Fiber calibration requires a flat build plate":"纤维校准需要平面打印盘。",
        "Fiber calibration needs separate base and fiber tools":"请为基材和纤维选择不同的物理喷头。",
        "Fiber calibration requires at least two values and a positive step":"步长须大于零，范围内至少需要两个测试值。",
        "Fiber trial spacing must be positive":"试线间距须大于零。",
        "Invalid fiber calibration value":"请输入有效的非负数值。",
        "Fiber calibration values allow at most three decimal places":"参数最多保留三位小数。",
        "Landing length or speed calibration requires positive height and descent speed":"扫描落点长度或速度时，落点高度和下降速度须大于零。",
        "Fiber calibration height exceeds the fiber tool limits":"落点最高位置超过了纤维喷头的可打印高度。",
        "Fiber calibration samples do not fit the build plate":"试线排布超出打印盘容量，请缩小间距或减少试线数量。",
        "Fiber calibration samples are too long for the build plate":"试线长度超出打印盘范围。",
        "Fiber calibration model does not fit the shared printable area":"校准样件超出两个喷头的共同可打印区域。",
        "Invalid pressure advance range":"压力提前范围无效，请核对起始值、结束值与步长。",
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
    return ({1:"压力提前",2:"压力提前",3:"压力提前",5:"流量比例",6:"温度",7:"最大体积流量",8:"VFA",9:"回抽",10:"输入整形 · 频率",11:"输入整形 · 阻尼",12:"转角",13:"公差测试模型",14:"纤维丝落点",15:"纤维丝温度"})[mode] || "校准"
}
function guidance(mode) {
    return ({1:"观察实物上挤出均匀的线，读取对应 PA 值。",2:"观察不同速度、加速度组合的转角，读取对应 PA 值。批量列表留空时使用原版默认。",3:"观察塔上转角与挤出变化，读取对应高度的 PA 值。",5:"观察实物顶面，选择线条连接均匀、表面平整的样块。",6:"从较高温度向较低温度测试；材料快捷范围只改测试温度，不切换材料预设。",7:"步长为原版每毫米塔高增加的体积流量系数。",8:"记录细纹明显或较平整的速度区间。",9:"观察回抽变化时的拉丝情况。",10:"观察实物 X/Y 方向振纹。设备参数的应用与保存由设备设置处理。",11:"在固定频率下观察阻尼变化。设备参数的应用与保存由设备设置处理。",12:"根据当前固件与 JD 配置显示转角单位和范围。",13:"载入 Orca 原版公差模型，无扫描参数；只记录配合观察。"})[mode] || ""
}
