import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtCore
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform  // 导入 GPlatform 以使用 ActionManager

Item {
    id: rightPart

    // 状态属性
    property string currentState: "idle"  // idle, generating, completed
    property int selectedModelIndex: -1
    property var pageRouter: null  // 页面路由器，用于跳转到3DPrint页面
    property int remainingSeconds: 3  // 倒计时秒数

    function resolveDemoModelPath(selectedModel) {
        if (selectedModel && selectedModel.filePath && selectedModel.filePath.length > 0) {
            return selectedModel.filePath
        }

        var downloadsPath = StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        if (!downloadsPath || downloadsPath.length === 0) {
            downloadsPath = StandardPaths.writableLocation(StandardPaths.HomeLocation)
        }
        return downloadsPath + "/GPlatform/Models/Dragon2.stl"
    }

    function extractFileName(filePath) {
        if (!filePath || filePath.length === 0) {
            return ""
        }

        var segments = filePath.split("/")
        return segments.length > 0 ? segments[segments.length - 1] : ""
    }

    // 状态变化处理
    onCurrentStateChanged: {
        if (currentState === "generating") {
            remainingSeconds = 3
            countdownTimer.start()
        } else if (currentState === "idle") {
            remainingSeconds = 3
        }
    }

    // 演示模型数据 - 各种龙
    property var demoModels: [
        { name: qsTr("Medieval Dragon"), color: "#8B4513" },
        { name: qsTr("Western Dragon"), color: "#228B22" },
        { name: qsTr("Eastern Dragon"), color: "#DC143C" },
        { name: qsTr("Coiled Dragon"), color: "#FFD700" },
        { name: qsTr("Fire Dragon"), color: "#FF4500" },
        { name: qsTr("Ice Dragon"), color: "#00CED1" }
    ]

    // 内容容器
    Item {
        anchors.fill: parent

        // 空闲状态
        Column {
            anchors.centerIn: parent
            spacing: 24
            visible: rightPart.currentState === "idle"

            // 3D立方体图标
            Canvas {
                width: 80
                height: 80
                anchors.horizontalCenter: parent.horizontalCenter

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.lineWidth = 2
                    ctx.strokeStyle = "#A096C0"
                    ctx.beginPath()

                    // 绘制3D立方体线框
                    ctx.moveTo(20, 20)
                    ctx.lineTo(60, 20)
                    ctx.lineTo(60, 60)
                    ctx.lineTo(20, 60)
                    ctx.closePath()

                    ctx.moveTo(30, 10)
                    ctx.lineTo(70, 10)
                    ctx.lineTo(70, 50)
                    ctx.lineTo(30, 50)
                    ctx.closePath()

                    ctx.moveTo(20, 20)
                    ctx.lineTo(30, 10)
                    ctx.moveTo(60, 20)
                    ctx.lineTo(70, 10)
                    ctx.moveTo(60, 60)
                    ctx.lineTo(70, 50)
                    ctx.moveTo(20, 60)
                    ctx.lineTo(30, 50)

                    ctx.stroke()
                }
            }

            Text {
                text: qsTr("Waiting to Generate")
                font.pixelSize: 24
                font.bold: true
                color: "#FFFFFF"
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: qsTr("Enter a description or upload reference images, then click Generate to create a 3D model")
                font.pixelSize: 14
                color: "#C0B8D9"
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                width: 280
            }
        }

        // 生成中状态
        Column {
            anchors.centerIn: parent
            spacing: 20
            visible: rightPart.currentState === "generating"

            // 旋转的加载动画
            Rectangle {
                width: 60
                height: 60
                color: "transparent"
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    width: 50
                    height: 50
                    anchors.centerIn: parent
                    radius: 25
                    color: "transparent"
                    border.color: "#4FC3F7"
                    border.width: 3

                    RotationAnimator on rotation {
                        from: 0
                        to: 360
                        duration: 1000
                        running: rightPart.currentState === "generating"
                        loops: Animation.Infinite
                    }
                }

                // 内部静态圆点
                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    color: "#4FC3F7"
                    anchors.centerIn: parent
                }
            }

            Text {
                text: qsTr("AI is generating the model...")
                font.pixelSize: 20
                font.bold: true
                color: "#FFFFFF"
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // 进度条
            Rectangle {
                width: 220
                height: 8
                color: "#3A3A5A"
                radius: 4
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    id: progressBar
                    width: 0
                    height: 8
                    color: "#4FC3F7"
                    radius: 4

                    NumberAnimation on width {
                        from: 0
                        to: 220
                        duration: 3000
                        running: rightPart.currentState === "generating"
                        easing.type: Easing.InOutQuad
                    }
                }
            }

            Text {
                id: countdownText
                text: qsTr("About %1 seconds remaining").arg(rightPart.remainingSeconds)
                font.pixelSize: 14
                color: "#B0B0D0"
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }

        // 倒计时计时器
        Timer {
            id: countdownTimer
            interval: 1000
            repeat: true
            running: rightPart.currentState === "generating"
            onTriggered: {
                if (rightPart.remainingSeconds > 0) {
                    rightPart.remainingSeconds--
                }
                if (rightPart.remainingSeconds <= 0) {
                    countdownTimer.stop()
                }
            }
        }

        // 生成完成状态 - 显示模型网格
        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16
            visible: rightPart.currentState === "completed"

            // 标题
            Text {
                text: qsTr("Generation complete! Select a model to import:")
                font.pixelSize: 16
                font.bold: true
                color: "#FFFFFF"
            }

            // 模型网格 - 2行3列
            Grid {
                width: parent.width
                columns: 3
                spacing: 12

                Repeater {
                    model: rightPart.demoModels

                    Item {
                        width: (parent.width - 24) / 3
                        height: 100

                        // 模型卡片
                        Rectangle {
                            id: modelCard
                            anchors.fill: parent
                            color: modelData.color
                            radius: 12
                            border.color: index === rightPart.selectedModelIndex ? "#4FC3F7" : "transparent"
                            border.width: index === rightPart.selectedModelIndex ? 3 : 0

                            // 模型图标
                            Text {
                                anchors.centerIn: parent
                                anchors.verticalCenterOffset: -10
                                text: "🐉"
                                font.pixelSize: 32
                            }

                            // 模型名称
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 8
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.name
                                font.pixelSize: 12
                                font.bold: true
                                color: "#FFFFFF"
                            }

                            // 选中标记
                            Rectangle {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 6
                                width: 20
                                height: 20
                                radius: 10
                                color: "#4FC3F7"
                                visible: index === rightPart.selectedModelIndex

                                Text {
                                    anchors.centerIn: parent
                                    text: "✓"
                                    color: "#FFFFFF"
                                    font.bold: true
                                    font.pixelSize: 12
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    rightPart.selectedModelIndex = index
                                }
                            }
                        }
                    }
                }
            }

            // 底部区域：提示文字和一键导入按钮
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 12

                // 提示文字
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Select a model above to view details, then click Import Now")
                    font.pixelSize: 12
                    color: "#A0A0C0"
                }

                // 一键导入按钮
                Rectangle {
                    id: importButton
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 120
                    height: 36
                    color: rightPart.selectedModelIndex !== -1 ? "#4FC3F7" : "#4A4A6A"
                    radius: 18

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Import Now")
                        color: rightPart.selectedModelIndex !== -1 ? "#FFFFFF" : "#8080A0"
                        font.pixelSize: 14
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor

                        onClicked: {
                            // 检查是否选中了模型
                            if (rightPart.selectedModelIndex === -1) {
                                console.log("请先选择一个模型")
                                return
                            }

                            var selectedModel = rightPart.demoModels[rightPart.selectedModelIndex]
                            console.log("导入模型: " + selectedModel.name)

                            // 执行模型导入操作（避免硬编码本机绝对路径）
                            var filePath = rightPart.resolveDemoModelPath(selectedModel)
                            var fileName = rightPart.extractFileName(filePath)
                            if (!filePath || filePath.length === 0) {
                                console.warn("未能解析导入模型路径")
                                return
                            }

                            console.log("开始导入模型: " + fileName)
                            ActionManager.triggerAction("import_model", {
                                "filePath": filePath,
                                "fileName": fileName
                            })

                            // 动态查找 router 并跳转到 3DPrint 页面
                            var p = rightPart.parent
                            while (p) {
                                if (p.router !== undefined) {
                                    console.log("找到 router，跳转到 /dock")
                                    p.router.go("/dock")
                                    return
                                }
                                if (p.pageRouter !== undefined) {
                                    console.log("找到 pageRouter，跳转到 /dock")
                                    p.pageRouter.go("/dock")
                                    return
                                }
                                p = p.parent
                            }
                            console.log("未找到 router 或 pageRouter")
                        }
                    }
                }
            }
        }
    }
}
