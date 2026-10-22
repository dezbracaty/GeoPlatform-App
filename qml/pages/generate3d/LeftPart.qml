import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl

Item {
    id: leftPart
    SmoothUI.theme: Theme.of(leftPart)

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
                    height: Theme.controlHeight
                    checkable: true
                    checked: leftPart.inputMode === "text"
                    highlighted: checked

                    onClicked: {
                        leftPart.inputMode = "text"
                    }
                }

                Button {
                    id: imageModeBtn
                    text: qsTr("Image Reference")
                    width: 130
                    height: Theme.controlHeight
                    checkable: true
                    checked: leftPart.inputMode === "image"
                    highlighted: checked

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
                    font: Typography.body
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    padding: Theme.controlHorizontalPadding
                }

                // 底部行：示例提示 + 字数统计
                RowLayout {
                    width: parent.width
                    height: 20

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("For example: a cute pink cartoon rabbit with long ears in a sitting pose")
                        font: Typography.caption
                        color: leftPart.SmoothUI.theme.res.textFillColorTertiary
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                        Layout.maximumWidth: parent.width - 70
                    }

                    Text {
                        id: wordCount
                        text: textArea.text.length + "/500"
                        font: Typography.caption
                        color: leftPart.SmoothUI.theme.res.textFillColorSecondary
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
                    color: leftPart.SmoothUI.theme.res.solidBackgroundFillColorSecondary
                    radius: leftPart.SmoothUI.radius
                    border.color: leftPart.SmoothUI.theme.res.controlStrokeColorDefault
                    border.width: 2

                    // 虚线边框效果
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: "transparent"
                        radius: Math.max(0, uploadZone.radius - 4)
                        border.color: leftPart.SmoothUI.theme.res.controlStrokeColorDefault
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
                            font: Typography.body
                            color: leftPart.SmoothUI.theme.res.textFillColorPrimary
                            anchors.horizontalCenter: parent.horizontalCenter
                        }

                        Text {
                            text: qsTr("Supports JPG, PNG, and WebP (up to 4 images)")
                            font: Typography.caption
                            color: leftPart.SmoothUI.theme.res.textFillColorTertiary
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }

                // 上传技巧提示
                Rectangle {
                    width: parent.width
                    height: 56
                    color: leftPart.SmoothUI.theme.res.solidBackgroundFillColorSecondary
                    radius: leftPart.SmoothUI.radius

                    Text {
                        id: tipsText
                        text: qsTr("💡 Tip: clear reference images produce better results")
                        anchors.centerIn: parent
                        font: Typography.caption
                        color: leftPart.SmoothUI.theme.res.textFillColorSecondary
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
                font: Typography.body
                color: leftPart.SmoothUI.theme.res.textFillColorSecondary
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
                        color: leftPart.SmoothUI.theme.res.solidBackgroundFillColorSecondary
                        radius: leftPart.SmoothUI.radius

                        Text {
                            text: modelData
                            anchors.centerIn: parent
                            color: leftPart.SmoothUI.theme.res.textFillColorPrimary
                            font: Typography.body
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

                Button {
                    id: clearButton
                    width: Math.max(140, implicitWidth)
                    height: Math.max(Theme.actionHeight, implicitHeight)
                    text: qsTr("Clear")
                    enabled: !leftPart.isGenerating
                             && (textArea.text.length > 0 || leftPart.inputMode === "image")
                    onClicked: textArea.text = ""
                }

                Button {
                    id: generateButton
                    width: Math.max(140, implicitWidth)
                    height: Math.max(Theme.actionHeight, implicitHeight)
                    text: leftPart.isGenerating ? qsTr("Generating...")
                                               : (leftPart.hasGenerated ? qsTr("Generate Again")
                                                                        : qsTr("Generate Model"))
                    highlighted: true
                    enabled: !leftPart.isGenerating
                    onClicked: {
                        leftPart.isGenerating = true
                        leftPart.generateClicked()
                        generateTimer.start()
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
