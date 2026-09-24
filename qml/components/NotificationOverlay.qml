import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

// Persistent bottom-right notifications. The visual shell intentionally follows
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
    readonly property bool latestIsFiberDebug: notifications.count > 0 && notifications.get(0).previewToken.length > 0

    SmoothUI.theme: Theme.of(root)

    implicitWidth: 360
    implicitHeight: 72
    radius: 10
    color: root.SmoothUI.theme.res.cardBackgroundFillColorDefault
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
        shadowColor: "#000000"
        shadowBlur: 0.5
        shadowOpacity: 0.18
        shadowVerticalOffset: 3
    }

    function pushSliceConfigurationError(presetName, message, optionKey) {
        // Orca keeps validation failures visible. Deduplicate an identical
        // diagnostic instead of stacking repeated slice attempts.
        for (let i = 0; i < notifications.count; ++i) {
            const item = notifications.get(i)
            if (item.message === message && item.optionKey === optionKey) {
                notifications.setProperty(i, "presetName", presetName)
                notifications.move(i, 0, 1)
                scheduleAutoDismiss()
                return
            }
        }

        notifications.insert(0, {
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
            if (notifications.get(i).previewToken.length > 0) notifications.remove(i)
        notifications.insert(0, {
            "presetName": "", "optionKey": "", "previewToken": previewToken,
            "title": qsTr("铺纤填充调试"),
            "message": qsTr("已采集 %1 项铺纤诊断，点击显示。青色：原始轮廓区域；红色：候选轮廓未生成区域；橙色：被拒绝路径。诊断内容不会打印。").arg(count)
        })
    }

    function activate(index) {
        if (index < 0 || index >= notifications.count)
            return
        const item = notifications.get(index)
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
                && notifications.get(0).previewToken.length === 0
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
        interval: root.autoDismissInterval
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
            color: root.SmoothUI.theme.res.systemFillColorCriticalBackground

            Icon {
                anchors.centerIn: parent
                width: 18
                height: 18
                source: root.latestIsFiberDebug ? FluentIcons.graph_Info : FluentIcons.graph_Error
                color: root.latestIsFiberDebug ? "#ff8000" : root.SmoothUI.theme.res.systemFillColorCritical
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
            color: root.SmoothUI.theme.res.systemFillColorCritical

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
            color: Theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
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
            if (root.latestIsFiberDebug) root.activate(0)
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
            radius: 10
            border.width: 1
            border.color: root.SmoothUI.theme.res.cardStrokeColorDefault

            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#000000"
                shadowBlur: 0.5
                shadowOpacity: 0.18
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
                    radius: 7
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
                            source: model.previewToken.length > 0 ? FluentIcons.graph_Info : FluentIcons.graph_Error
                            color: model.previewToken.length > 0 ? "#ff8000" : root.SmoothUI.theme.res.systemFillColorCritical
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
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
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
