.pragma library

var printerModel = [
    {
        name: "X1 Carbon",
        status: "Ready",
        temperature: 25,
        progress: 0,
        selected: true,
        lastPrint: "2h ago",
        type: "FDM",
        buildVolume: "256x256x256mm"
    },
    {
        name: "Prusa MK4",
        status: "Printing",
        temperature: 210,
        progress: 65,
        selected: false,
        lastPrint: "Currently printing",
        type: "FDM",
        buildVolume: "250x210x210mm"
    },
    {
        name: "Ender 3 V3",
        status: "Offline",
        temperature: 20,
        progress: 0,
        selected: false,
        lastPrint: "3d ago",
        type: "FDM",
        buildVolume: "220x220x250mm"
    },
    {
        name: "Bambu P1S",
        status: "Maintenance",
        temperature: 22,
        progress: 0,
        selected: false,
        lastPrint: "Service due",
        type: "FDM",
        buildVolume: "256x256x256mm"
    }
]

var printQueue = [
    {
        id: 1,
        modelName: "Phone Stand",
        fileName: "phone_stand_v2.gcode",
        printer: "X1 Carbon",
        status: "Queued",
        estimatedTime: "1h 45m",
        material: "PLA",
        color: "#FF6B6B",
        priority: 1
    },
    {
        id: 2,
        modelName: "Gear Set",
        fileName: "gear_set_final.gcode",
        printer: "Prusa MK4",
        status: "Printing",
        estimatedTime: "2h 30m",
        material: "ABS",
        color: "#4ECDC4",
        priority: 2,
        progress: 65
    },
    {
        id: 3,
        modelName: "Vase Design",
        fileName: "artistic_vase.gcode",
        printer: "X1 Carbon",
        status: "Queued",
        estimatedTime: "3h 20m",
        material: "PETG",
        color: "#95E1D3",
        priority: 3
    }
]

var recentModels = [
    {
        name: "Mechanical Gear",
        size: "2.3 MB",
        lastModified: "2 hours ago",
        printCount: 5,
        successRate: 100,
        thumbnail: "",
        category: "Engineering"
    },
    {
        name: "Phone Stand Pro",
        size: "1.8 MB",
        lastModified: "Yesterday",
        printCount: 3,
        successRate: 66.7,
        thumbnail: "",
        category: "Accessories"
    },
    {
        name: "Artistic Vase",
        size: "4.1 MB",
        lastModified: "3 days ago",
        printCount: 2,
        successRate: 100,
        thumbnail: "",
        category: "Art"
    },
    {
        name: "Tool Organizer",
        size: "980 KB",
        lastModified: "Last week",
        printCount: 8,
        successRate: 87.5,
        thumbnail: "",
        category: "Organization"
    },
    {
        name: "Desk Lamp Base",
        size: "3.2 MB",
        lastModified: "Last week",
        printCount: 1,
        successRate: 100,
        thumbnail: "",
        category: "Furniture"
    }
]

var statistics = {
    totalPrints: 127,
    successfulPrints: 120,
    failedPrints: 7,
    totalMaterialUsed: 15.7, // kg
    totalPrintTime: 342.5, // hours
    averageSuccessRate: 94.5,
    popularMaterial: "PLA",
    busiestDay: "Monday",
    averagePrintTime: 2.7 // hours
}

function initialize() {
    console.log("PrinterManager initialized")
    console.log("Printers:", printerModel.length)
    console.log("Queue items:", printQueue.length)
}

function getPrinter(index) {
    if (index >= 0 && index < printerModel.length) {
        return printerModel[index]
    }
    return null
}

function updatePrinterStatus(printerName, status) {
    for (var i = 0; i < printerModel.length; i++) {
        if (printerModel[i].name === printerName) {
            printerModel[i].status = status
            return true
        }
    }
    return false
}

function addToQueue(modelData) {
    var newItem = {
        id: printQueue.length + 1,
        modelName: modelData.name || "Untitled",
        fileName: modelData.fileName || "unknown.gcode",
        printer: modelData.printer || "Auto",
        status: "Queued",
        estimatedTime: modelData.estimatedTime || "Unknown",
        material: modelData.material || "PLA",
        color: modelData.color || "#808080",
        priority: printQueue.length + 1
    }
    printQueue.push(newItem)
    return newItem.id
}

function removeFromQueue(id) {
    for (var i = 0; i < printQueue.length; i++) {
        if (printQueue[i].id === id) {
            printQueue.splice(i, 1)
            return true
        }
    }
    return false
}

function getStatistics() {
    return statistics
}

// 获取当前选中的打印机
function getSelectedPrinter() {
    for (var i = 0; i < printerModel.length; i++) {
        if (printerModel[i].selected === true) {
            return printerModel[i]
        }
    }
    return null
}

// 获取当前选中的打印机名称
function getSelectedPrinterName() {
    var printer = getSelectedPrinter()
    return printer ? printer.name : ""
}

// 选择打印机（按索引）
function selectPrinter(index) {
    // 取消所有选择
    for (var i = 0; i < printerModel.length; i++) {
        printerModel[i].selected = false
    }
    // 选中指定的打印机
    if (index >= 0 && index < printerModel.length) {
        printerModel[index].selected = true
        return true
    }
    return false
}

// 选择打印机（按名称）
function selectPrinterByName(name) {
    for (var i = 0; i < printerModel.length; i++) {
        printerModel[i].selected = (printerModel[i].name === name)
    }
}

function calculateStorageUsage() {
    var totalSize = 0
    for (var i = 0; i < recentModels.length; i++) {
        var sizeStr = recentModels[i].size
        var size = parseFloat(sizeStr)
        if (sizeStr.includes("MB")) {
            totalSize += size
        } else if (sizeStr.includes("KB")) {
            totalSize += size / 1024
        } else if (sizeStr.includes("GB")) {
            totalSize += size * 1024
        }
    }
    return totalSize // in MB
}