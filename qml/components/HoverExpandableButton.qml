import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Item {
    id: root
    SmoothUI.theme: Theme.of(root)

    // 公共属性
    property alias icon: mainButton.icon
    property alias checked: mainButton.checked
    property string tooltip: ""

    // 展开窗口的配置
    property var expandItems: []  // 展开项列表: [{icon, text, action, enabled, shortcut}]
    property int expandDirection: 0  // 0=右, 1=下, 2=左, 3=上
    property int hoverDelay: 300     // hover延迟毫秒
    property int hideDelay: 500      // 隐藏延迟毫秒

    implicitWidth: PrintWorkspaceStyle.toolbarButtonSize
    implicitHeight: PrintWorkspaceStyle.toolbarButtonSize

    // 主按钮
    IconButton {
        id: mainButton
        objectName: root.objectName.length > 0 ? root.objectName + ".button" : ""
        anchors.fill: parent
        hoverEnabled: true
        highlighted: enabled && (checked || expandPopup.opened)
        icon.width: 20
        icon.height: 20

        onHoveredChanged: {
            root.isButtonHovered = hovered && root.enabled
            if (!root.enabled)
                return
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

    onEnabledChanged: {
        if (!enabled) {
            if (showTimer) showTimer.stop()
            if (hideTimer) hideTimer.stop()
            isButtonHovered = false
            isPopupHovered = false
            hoverCount = 0
            if (expandPopup) expandPopup.close()
        }
    }

    // 显示计时器
    Timer {
        id: showTimer
        interval: root.hoverDelay
        onTriggered: {
            if (root.enabled && mainButton.hovered && root.expandItems.length > 0) {
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
        SmoothUI.dark: root.SmoothUI.dark
        SmoothUI.primaryColor: root.SmoothUI.primaryColor
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
                    if (!root.enabled) return
                    root.isPopupHovered = true
                    hideTimer.stop()
                }
                onExited: {
                    if (!root.enabled) return
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

                    delegate: ItemDelegate {
                        id: expandItemButton
                        objectName: itemData.targetName || ""
                        Layout.fillWidth: true
                        Layout.preferredHeight: PrintWorkspaceStyle.popoverRowHeight
                        Layout.minimumWidth: 136
                        Layout.maximumWidth: 232

                        property var itemData: modelData

                        enabled: root.enabled && (itemData.enabled !== undefined ? itemData.enabled : true)
                        hoverEnabled: true

                        // 使用计数器来跟踪 hover 状态
                        onHoveredChanged: {
                            if (!root.enabled) return
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

                        contentItem: RowLayout {
                            spacing: 8

                            // 图标
                            Icon {
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                source: itemData.icon || ""
                                color: expandItemButton.SmoothUI.textColor
                            }

                            // 文字
                            Label {
                                Layout.fillWidth: true
                                text: itemData.text || ""
                                font: Typography.body
                                color: expandItemButton.SmoothUI.textColor
                                elide: Text.ElideRight
                            }

                            // 快捷键
                            Label {
                                visible: itemData.shortcut !== undefined
                                text: itemData.shortcut || ""
                                font: Typography.caption
                                color: expandItemButton.enabled
                                       ? expandItemButton.SmoothUI.theme.res.textFillColorSecondary
                                       : expandItemButton.SmoothUI.theme.res.textFillColorDisabled
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
                            visible: parent.hovered && !!itemData.tooltip
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
