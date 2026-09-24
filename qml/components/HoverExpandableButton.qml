import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import GPlatform

Item {
    id: root

    // 公共属性
    property alias icon: mainButton.icon
    property alias enabled: mainButton.enabled
    property string tooltip: ""

    // 展开窗口的配置
    property var expandItems: []  // 展开项列表: [{icon, text, action, enabled, shortcut}]
    property int expandDirection: 0  // 0=右, 1=下, 2=左, 3=上
    property int hoverDelay: 300     // hover延迟毫秒
    property int hideDelay: 500      // 隐藏延迟毫秒

    implicitWidth: PrintWorkspaceStyle.toolbarButtonSize
    implicitHeight: PrintWorkspaceStyle.toolbarButtonSize

    // 主按钮背景 - hover 效果
    Rectangle {
        anchors.fill: parent
        color: expandPopup.opened || mainButton.hovered
               ? Theme.res.subtleFillColorSecondary
               : "transparent"
        radius: 8
        z: -1
    }

    // 主按钮
    IconButton {
        id: mainButton
        objectName: root.objectName.length > 0 ? root.objectName + ".button" : ""
        anchors.fill: parent
        hoverEnabled: true
        icon.width: 20
        icon.height: 20
        backgroundColor: Qt.rgba(0,0,0,0)  // 始终透明，去掉 disabled 时的白色背景

        onHoveredChanged: {
            root.isButtonHovered = hovered
            if (hovered) {
                showTimer.start()
                hideTimer.stop()
            } else {
                showTimer.stop()
                // 延迟检查是否真的需要隐藏
                hideTimer.start()
            }
        }

        // 工具提示
        ToolTip {
            visible: parent.hovered && root.tooltip !== "" && !expandPopup.visible
            text: root.tooltip
            delay: 1000
        }
    }

    // 状态标记
    property bool isButtonHovered: false
    property bool isPopupHovered: false
    property int hoverCount: 0  // 记录有多少个按钮正在被 hover

    // 显示计时器
    Timer {
        id: showTimer
        interval: root.hoverDelay
        onTriggered: {
            if (root.expandItems.length > 0) {
                expandPopup.open()
            }
        }
    }

    // 隐藏计时器
    Timer {
        id: hideTimer
        interval: root.hideDelay
        onTriggered: {
            // 只有在两个区域都不悬停时才关闭
            if (!root.isButtonHovered && !root.isPopupHovered) {
                expandPopup.close()
            }
        }
    }

    // 展开弹窗
    Popup {
        id: expandPopup

        parent: root
        padding: PrintWorkspaceStyle.space4
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // 监听弹窗关闭
        onClosed: {
            root.isPopupHovered = false
            hoverCount = 0
        }

        // 根据方向计算位置
        x: {
            switch (root.expandDirection) {
                case 0: return root.width + 6
                case 2: return -width - 6
                default: return 0                // 上下
            }
        }

        y: {
            switch (root.expandDirection) {
                case 1: return root.height + 6
                case 3: return -height - 6
                default: return 0                // 左右
            }
        }

        // 背景和样式
        background: PopoverSurface {

            // 捕获背景板的 hover 事件
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: {
                    root.isPopupHovered = true
                    hideTimer.stop()
                }
                onExited: {
                    // 只有在所有区域都不 hover 时才关闭
                    if (root.hoverCount <= 0 && !root.isButtonHovered) {
                        root.isPopupHovered = false
                        hideTimer.start()
                    }
                }
            }
        }

        // 内容区域
        contentItem: Item {
            implicitWidth: column.implicitWidth
            implicitHeight: column.implicitHeight

            ColumnLayout {
                id: column
                anchors.fill: parent
                spacing: 2

                Repeater {
                    model: root.expandItems

                    delegate: Button {
                        objectName: itemData.targetName || ""
                        Layout.fillWidth: true
                        Layout.preferredHeight: PrintWorkspaceStyle.popoverRowHeight
                        Layout.minimumWidth: 136
                        Layout.maximumWidth: 232
                        leftPadding: 10
                        rightPadding: 10

                        property var itemData: modelData

                        enabled: itemData.enabled !== undefined ? itemData.enabled : true
                        hoverEnabled: true

                        // 使用计数器来跟踪 hover 状态
                        onHoveredChanged: {
                            if (hovered) {
                                root.hoverCount++
                                root.isPopupHovered = true
                                hideTimer.stop()
                            } else {
                                root.hoverCount--
                                // 只有当所有按钮都不被 hover 时，才启动隐藏计时器
                                if (root.hoverCount <= 0) {
                                    root.hoverCount = 0
                                    root.isPopupHovered = false
                                    if (!root.isButtonHovered) {
                                        hideTimer.start()
                                    }
                                }
                            }
                        }

                        background: Rectangle {
                            color: parent.hovered
                                   ? Theme.res.subtleFillColorSecondary
                                   : "transparent"
                            radius: PrintWorkspaceStyle.controlRadius
                        }

                        contentItem: RowLayout {
                            spacing: 8

                            // 图标
                            Icon {
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                source: itemData.icon || ""
                                color: parent.parent.enabled ? Theme.res.textFillColorPrimary : Theme.res.textFillColorDisabled
                            }

                            // 文字
                            Label {
                                Layout.fillWidth: true
                                text: itemData.text || ""
                                font: Typography.body
                                color: parent.parent.enabled ? Theme.res.textFillColorPrimary : Theme.res.textFillColorDisabled
                                elide: Text.ElideRight
                            }

                            // 快捷键
                            Label {
                                visible: itemData.shortcut !== undefined
                                text: itemData.shortcut || ""
                                font: Typography.caption
                                color: Theme.res.textFillColorSecondary
                            }
                        }

                        onClicked: {
                            expandPopup.close()
                            if (itemData.action && typeof itemData.action === "function") {
                                itemData.action()
                            }
                        }

                        // 工具提示
                        ToolTip {
                            visible: parent.hovered && itemData.tooltip
                            text: itemData.tooltip || ""
                            delay: 1000
                        }
                    }
                }
            }
        }

        // 进入/退出动画
        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 120
                easing.type: Easing.OutQuart
            }
        }

        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: 100
                easing.type: Easing.InQuart
            }
        }

    }
}
