import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

// Timed bottom-right notifications. The visual shell intentionally follows
// the former BackgroundTaskOverlay: a compact pill that expands upward into a
// non-modal list. Producers only submit data; opening details remains a user
// action.
Rectangle {
    id: root

    signal sliceConfigurationErrorActivated(string presetName,
                                             string message,
                                             string optionKey)
    signal fiberFillDebugActivated(string previewToken)

    readonly property int autoDismissInterval: 10000
    readonly property string latestKind: notifications.count > 0 ? notifications.get(0).kind : ""
    readonly property bool latestIsFiberDebug: latestKind === "fiberDebug"

    SmoothUI.theme: Theme.of(root)

    implicitWidth: 360
    implicitHeight: 72
    radius: Theme.panelRadius
    color: root.SmoothUI.theme.res.cardBackgroundColor
    border.width: 1
    border.color: root.SmoothUI.theme.res.cardStrokeColorDefault

    visible: notifications.count > 0 || opacity > 0.01
    opacity: notifications.count > 0 ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
    }

    layer.enabled: visible
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: root.SmoothUI.theme.res.shadowColor
        shadowBlur: 0.5
        shadowOpacity: Theme.shadowOpacityFor(root.SmoothUI.dark, 0.18)
        shadowVerticalOffset: 3
    }

    function notificationIcon(kind) {
        if (kind === "warning") return FluentIcons.graph_Warning
        if (kind === "fiberDebug") return FluentIcons.graph_Info
        return FluentIcons.graph_Error
    }

    function notificationColor(kind) {
        if (kind === "warning") return root.SmoothUI.theme.res.systemFillColorCaution
        if (kind === "fiberDebug") return root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
        return root.SmoothUI.theme.res.systemFillColorCritical
    }

    function pushWarning(title, message) {
        for (let i = 0; i < notifications.count; ++i) {
            const item = notifications.get(i)
            if (item.kind === "warning" && item.title === title && item.message === message) {
                notifications.move(i, 0, 1)
                scheduleAutoDismiss()
                return
            }
        }
        notifications.insert(0, {
            "kind": "warning", "presetName": "", "optionKey": "", "previewToken": "",
            "title": title, "message": message
        })
    }

    function pushSliceConfigurationError(presetName, message, optionKey) {
        // Orca keeps validation failures visible. Deduplicate an identical
        // diagnostic instead of stacking repeated slice attempts.
        for (let i = 0; i < notifications.count; ++i) {
            const item = notifications.get(i)
            if (item.kind === "configurationError" && item.message === message && item.optionKey === optionKey) {
                notifications.setProperty(i, "presetName", presetName)
                notifications.move(i, 0, 1)
                scheduleAutoDismiss()
                return
            }
        }

        notifications.insert(0, {
            "kind": "configurationError",
            "presetName": presetName,
            "message": message,
            "optionKey": optionKey,
            "previewToken": "",
            "title": qsTr("切片失败")
        })
    }

    function pushFiberFillDebug(previewToken, count) {
        // Only the current slice can be activated; replace older debug notices.
        for (let i = notifications.count - 1; i >= 0; --i)
            if (notifications.get(i).kind === "fiberDebug") notifications.remove(i)
        notifications.insert(0, {
            "kind": "fiberDebug",
            "presetName": "", "optionKey": "", "previewToken": previewToken,
            "title": qsTr("铺纤填充调试"),
            "message": count > 0
                ? qsTr("已采集 %1 项铺纤诊断，点击显示。青色：原始轮廓区域；红色：候选轮廓未生成区域；橙色：被拒绝路径。诊断内容不会打印。").arg(count)
                : qsTr("没有可显示的铺纤诊断。未生成区域仅比较已有候选轮廓与最终纤维覆盖，不包含从未生成候选的部分。")
        })
    }

    function activate(index) {
        if (index < 0 || index >= notifications.count)
            return
        const item = notifications.get(index)
        if (item.kind === "warning" || (item.kind === "fiberDebug" && item.previewToken.length === 0))
            return
        const presetName = item.presetName
        const message = item.message
        const optionKey = item.optionKey
        const previewToken = item.previewToken
        notificationListPopup.close()
        // Opening the details acknowledges this notification. Do not leave a
        // second copy behind that the user has to dismiss again.
        notifications.remove(index)
        if (previewToken.length > 0)
            root.fiberFillDebugActivated(previewToken)
        else
            root.sliceConfigurationErrorActivated(presetName, message, optionKey)
    }

    function dismiss(index) {
        if (index < 0 || index >= notifications.count)
            return
        notifications.remove(index)
        if (notifications.count === 0)
            notificationListPopup.close()
    }

    function scheduleAutoDismiss() {
        if (notifications.count > 0
                && !notificationListPopup.opened
                && !notificationHover.hovered) {
            autoDismissTimer.restart()
        } else {
            autoDismissTimer.stop()
        }
    }

    ListModel {
        id: notifications

        onCountChanged: root.scheduleAutoDismiss()
    }

    Timer {
        id: autoDismissTimer
        interval: root.latestKind === "warning" ? 4000 : root.autoDismissInterval
        repeat: false
        onTriggered: root.dismiss(0)
    }

    HoverHandler {
        id: notificationHover
        onHoveredChanged: root.scheduleAutoDismiss()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 12
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: 18
            color: root.latestKind === "warning"
                   ? root.SmoothUI.theme.res.systemFillColorCautionBackground
                   : root.latestIsFiberDebug
                     ? root.SmoothUI.theme.res.systemFillColorSolidAttentionBackground
                     : root.SmoothUI.theme.res.systemFillColorCriticalBackground

            Icon {
                anchors.centerIn: parent
                width: 18
                height: 18
                source: root.notificationIcon(root.latestKind)
                color: root.notificationColor(root.latestKind)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: notifications.count > 0 ? notifications.get(0).title : ""
                elide: Text.ElideRight
                font: Typography.bodyStrong
                color: root.SmoothUI.theme.res.textFillColorPrimary
            }

            Label {
                Layout.fillWidth: true
                text: notifications.count > 0 ? notifications.get(0).message : ""
                textFormat: Text.PlainText
                maximumLineCount: 1
                elide: Text.ElideRight
                font: Typography.caption
                color: root.SmoothUI.theme.res.textFillColorSecondary
            }
        }

        Rectangle {
            visible: notifications.count > 1
            implicitWidth: Math.max(20, countLabel.implicitWidth + 8)
            implicitHeight: 20
            radius: 10
            color: root.notificationColor(root.latestKind)

            Label {
                id: countLabel
                anchors.centerIn: parent
                text: notifications.count.toString()
                color: "#ffffff"
                font: Typography.caption
            }
        }

        Label {
            text: qsTr("查看")
            font: Typography.caption
            color: root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
        }

        IconButton {
            id: dismissLatestButton
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            icon.name: FluentIcons.graph_ChromeClose
            icon.width: 11
            icon.height: 11
            onClicked: root.dismiss(0)

            ToolTip {
                text: qsTr("关闭")
                visible: parent.hovered
                delay: Theme.tooltipDelay
            }
        }
    }

    MouseArea {
        id: pillClickArea
        anchors.fill: parent
        anchors.rightMargin: dismissLatestButton.width + 12
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (root.latestIsFiberDebug && notifications.get(0).previewToken.length > 0) root.activate(0)
            else notificationListPopup.toggle()
        }
    }

    Popup {
        id: notificationListPopup

        parent: root
        width: root.width
        x: 0
        y: -height - 8
        padding: 8
        modal: false
        dim: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property double lastClosedMs: 0

        function toggle() {
            if (opened) {
                close()
            } else if (Date.now() - lastClosedMs > 200) {
                open()
            }
        }

        onAboutToHide: lastClosedMs = Date.now()
        onOpened: {
            autoDismissTimer.stop()
            forceActiveFocus()
        }
        onClosed: {
            root.scheduleAutoDismiss()
        }

        background: Rectangle {
            color: root.SmoothUI.theme.res.solidBackgroundFillColorBase
            radius: Theme.popupRadius
            border.width: 1
            border.color: root.SmoothUI.theme.res.cardStrokeColorDefault

            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: root.SmoothUI.theme.res.shadowColor
                shadowBlur: 0.5
                shadowOpacity: Theme.shadowOpacityFor(root.SmoothUI.dark, 0.18)
                shadowVerticalOffset: 3
            }
        }

        contentItem: ColumnLayout {
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 6
                Layout.rightMargin: 4
                Layout.topMargin: 4
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: qsTr("通知")
                    font: Typography.bodyStrong
                    color: root.SmoothUI.theme.res.textFillColorPrimary
                }

                Label {
                    text: notifications.count.toString()
                    font: Typography.caption
                    color: root.SmoothUI.theme.res.textFillColorSecondary
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: root.SmoothUI.theme.res.dividerStrokeColorDefault
            }

            ListView {
                id: notificationList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 280)
                clip: true
                spacing: 4
                model: notifications
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: notificationItem
                    width: ListView.view ? ListView.view.width : 0
                    implicitHeight: itemContent.implicitHeight + 20
                    radius: root.SmoothUI.radius
                    color: itemMouse.hovered
                           ? root.SmoothUI.theme.res.subtleFillColorSecondary
                           : "transparent"

                    RowLayout {
                        id: itemContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        spacing: 10

                        Icon {
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            Layout.alignment: Qt.AlignTop
                            source: root.notificationIcon(model.kind)
                            color: root.notificationColor(model.kind)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Label {
                                Layout.fillWidth: true
                                text: model.title
                                elide: Text.ElideRight
                                font: Typography.bodyStrong
                                color: root.SmoothUI.theme.res.textFillColorPrimary
                            }

                            Label {
                                Layout.fillWidth: true
                                text: model.message
                                textFormat: model.kind === "warning" ? Text.PlainText : Text.AutoText
                                wrapMode: Text.WordWrap
                                // Warnings have no modal details dialog. Let the user
                                // read the full text inside the bounded, scrollable list.
                                maximumLineCount: model.kind === "warning" ? 2147483647 : 2
                                elide: Text.ElideRight
                                font: Typography.caption
                                color: root.SmoothUI.theme.res.textFillColorSecondary
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: model.presetName.length > 0
                                text: qsTr("当前：%1").arg(model.presetName)
                                elide: Text.ElideMiddle
                                font: Typography.caption
                                color: root.SmoothUI.theme.res.textFillColorTertiary
                            }

                            Label {
                                visible: model.kind === "configurationError" || model.previewToken.length > 0
                                text: qsTr("查看详情")
                                font: Typography.caption
                                color: Theme.accentColor.defaultBrushFor(
                                           root.SmoothUI.dark)
                            }
                        }

                        IconButton {
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            Layout.alignment: Qt.AlignTop
                            icon.name: FluentIcons.graph_ChromeClose
                            icon.width: 11
                            icon.height: 11
                            onClicked: root.dismiss(index)

                            ToolTip {
                                text: qsTr("关闭")
                                visible: parent.hovered
                                delay: Theme.tooltipDelay
                            }
                        }
                    }

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        anchors.rightMargin: 34
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activate(index)
                    }
                }
            }
        }

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: 130
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1.0
                duration: 130
                easing.type: Easing.OutCubic
            }
        }

        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: 90
                easing.type: Easing.InCubic
            }
        }
    }
}
