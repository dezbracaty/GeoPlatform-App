import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl

Item {
    id: leftPart

    // 输入模式属性
    property string inputMode: "text"
    onInputModeChanged: {
        textInputArea.visible = inputMode === "text"
        imageUploadArea.visible = inputMode === "image"
    }

    // 选中样式属性
    property var selectedStyles: []

    // 生成状态属性
    property bool isGenerating: false
    property bool hasGenerated: false  // 是否已经生成过

    // 信号
    signal generateClicked()
    signal generationCompleted()

    // 内容容器 - 直接填充，无滚动
    Column {
        id: contentColumn
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: 10

        // 模式切换区域
        Item {
            width: parent.width
            height: 42

            Row {
                id: modeSwitcher
                anchors.centerIn: parent
                spacing: 10

                Button {
                    id: textModeBtn
                    text: qsTr("Text Description")
                    width: 130
                    height: 36
                    checkable: true
                    checked: leftPart.inputMode === "text"

                    background: Rectangle {
                        radius: 20
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: parent.checked ? "#4FC3F7" : "#3D2F5A" }
                            GradientStop { position: 1.0; color: parent.checked ? "#3986E5" : "#3D2F5A" }
                        }
                        border.color: parent.checked ? "#5BC0DE" : "#5A4A7A"
                        border.width: 1
                    }

                    contentItem: Text {
                        text: parent.text
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.bold: parent.checked
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        leftPart.inputMode = "text"
                    }
                }

                Button {
                    id: imageModeBtn
                    text: qsTr("Image Reference")
                    width: 130
                    height: 36
                    checkable: true
                    checked: leftPart.inputMode === "image"

                    background: Rectangle {
                        radius: 20
                        color: parent.checked ? "#8B5CF6" : "#3D2F5A"
                        border.color: parent.checked ? "#A78BFA" : "#5A4A7A"
                        border.width: 1
                    }

                    contentItem: Text {
                        text: parent.text
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.bold: parent.checked
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        leftPart.inputMode = "image"
                    }
                }
            }
        }

        // 文本输入区域
        Item {
            id: textInputArea
            width: parent.width
            height: 200
            visible: leftPart.inputMode === "text"

            Column {
                anchors.fill: parent
                spacing: 10

                // 文本描述输入框
                TextArea {
                    id: textArea
                    width: parent.width
                    height: 140
                    placeholderText: qsTr("Describe the 3D model you want to generate...")
                    placeholderTextColor: "#7A7090"
                    font.pixelSize: 14
                    color: "#FFFFFF"
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    padding: 12

                    background: Rectangle {
                        color: "#251B3D"
                        radius: 12
                        border.color: "#4A3D6A"
                        border.width: 1
                    }
                }

                // 底部行：示例提示 + 字数统计
                RowLayout {
                    width: parent.width
                    height: 20

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("For example: a cute pink cartoon rabbit with long ears in a sitting pose")
                        font.pixelSize: 12
                        color: "#7A7090"
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                        Layout.maximumWidth: parent.width - 70
                    }

                    Text {
                        id: wordCount
                        text: textArea.text.length + "/500"
                        font.pixelSize: 12
                        color: "#9A90B0"
                    }
                }
            }
        }

        // 图片上传区域
        Item {
            id: imageUploadArea
            width: parent.width
            height: 200
            visible: leftPart.inputMode === "image"

            Column {
                anchors.fill: parent
                spacing: 12

                // 拖拽上传区域
                Rectangle {
                    id: uploadZone
                    width: parent.width
                    height: 130
                    color: "#251B3D"
                    radius: 12
                    border.color: "#6A50D0"
                    border.width: 2

                    // 虚线边框效果
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: "transparent"
                        radius: 8
                        border.color: "#5A4A8A"
                        border.width: 2
                    }

                    // 图标和文字
                    Column {
                        anchors.centerIn: parent
                        spacing: 8

                        Text {
                            text: qsTr("📤")
                            font.pixelSize: 36
                            anchors.horizontalCenter: parent.horizontalCenter
                        }

                        Text {
                            text: qsTr("Click or drag to upload images")
                            font.pixelSize: 14
                            color: "#C0B0E0"
                            anchors.horizontalCenter: parent.horizontalCenter
                        }

                        Text {
                            text: qsTr("Supports JPG, PNG, and WebP (up to 4 images)")
                            font.pixelSize: 12
                            color: "#8A80A0"
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }

                // 上传技巧提示
                Rectangle {
                    width: parent.width
                    height: 56
                    color: "#3D2F5A"
                    radius: 10

                    Text {
                        id: tipsText
                        text: qsTr("💡 Tip: clear reference images produce better results")
                        anchors.centerIn: parent
                        font.pixelSize: 12
                        color: "#C0B0E0"
                        wrapMode: Text.Wrap
                        width: parent.width - 20
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        // 灵感提示区域 - 垂直单列布局
        Column {
            width: parent.width
            spacing: 12

            Text {
                text: qsTr("Inspiration")
                font.pixelSize: 14
                font.bold: false
                color: "#A096C0"
            }

            // 垂直单列排列的灵感卡片
            Column {
                width: parent.width
                spacing: 12

                Repeater {
                    model: [qsTr("A cute cartoon kitten with big eyes in a sitting pose"), qsTr("A proud Western dragon with spread wings and shimmering scales"), qsTr("A medieval castle with towers and a moat"), qsTr("A humanoid sci-fi robot with a metallic finish and glowing blue eyes")]

                    Rectangle {
                        width: parent.width
                        height: 40
                        color: Qt.rgba(80/255, 60/255, 120/255, 0.3)
                        radius: 12

                        Text {
                            text: modelData
                            anchors.centerIn: parent
                            color: "#E0D8F0"
                            font.pixelSize: 14
                            horizontalAlignment: Text.AlignHCenter
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                textArea.text = "\"" + modelData + "\""
                                textArea.forceActiveFocus()
                            }
                        }
                    }
                }
            }
        }

        // 操作按钮区域
        Item {
            width: parent.width
            height: 60

            Row {
                anchors.centerIn: parent
                spacing: 16

                // 清除按钮 - 使用 Rectangle 确保大小��致
                Rectangle {
                    id: clearButton
                    width: 140
                    height: 44
                    radius: 8
                    color: clearButtonEnabled ? "#3D2F5A" : "#2A2040"
                    border.color: clearButtonEnabled ? "#5A4A7A" : "#3D2F5A"
                    border.width: 1

                    property bool clearButtonEnabled: !leftPart.isGenerating && (textArea.text.length > 0 || leftPart.inputMode === "image")

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Clear")
                        color: clearButton.clearButtonEnabled ? "#C0B0E0" : "#5A4A6A"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: clearButton.clearButtonEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            if (clearButton.clearButtonEnabled) {
                                textArea.text = ""
                            }
                        }
                    }
                }

                // 生成模型按钮 - 使用 Rectangle 确保大小一致
                Rectangle {
                    id: generateButton
                    width: 140
                    height: 44
                    radius: 8
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: leftPart.isGenerating ? "#7A8A9A" : "#4FC3F7" }
                        GradientStop { position: 1.0; color: leftPart.isGenerating ? "#6A7A8A" : "#3986E5" }
                    }
                    border.color: leftPart.isGenerating ? "#8A9AAA" : "#5BC0DE"
                    border.width: 1

                    Row {
                        anchors.centerIn: parent
                        spacing: 6

                        Text {
                            text: leftPart.isGenerating ? qsTr("⏳") : qsTr("✨")
                            font.pixelSize: 16
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: leftPart.isGenerating ? qsTr("Generating...") : (leftPart.hasGenerated ? qsTr("Generate Again") : qsTr("Generate Model"))
                            color: "#FFFFFF"
                            font.pixelSize: 14
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: leftPart.isGenerating ? Qt.BusyCursor : Qt.PointingHandCursor
                        onClicked: {
                            if (!leftPart.isGenerating) {
                                leftPart.isGenerating = true
                                leftPart.generateClicked()
                                generateTimer.start()
                            }
                        }
                    }
                }

                // 生成计时器
                Timer {
                    id: generateTimer
                    interval: 3000
                    repeat: false
                    onTriggered: {
                        leftPart.hasGenerated = true
                        leftPart.generationCompleted()
                    }
                }
            }
        }
    }
}
