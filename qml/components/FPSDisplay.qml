import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl

Item {
    id: root
    SmoothUI.theme: Theme.of(root)

    property var fpsMonitor: null
    property int targetRefreshRate: 60
    property bool expanded: false

    implicitWidth: expanded ? Math.max(260, details.implicitWidth + 24) : compactButton.implicitWidth
    implicitHeight: expanded ? details.implicitHeight + 24 : compactButton.implicitHeight
    width: parent ? Math.min(implicitWidth, Math.max(0, parent.width - 32)) : implicitWidth
    height: parent ? Math.min(implicitHeight, Math.max(0, parent.height - 32)) : implicitHeight

    PanelSurface {
        anchors.fill: parent
        SmoothUI.dark: root.SmoothUI.dark
        SmoothUI.primaryColor: root.SmoothUI.primaryColor
    }

    Behavior on width {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }
    Behavior on height {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    Button {
        id: compactButton
        visible: !root.expanded
        anchors.fill: parent
        flat: true
        padding: 7
        clip: true
        background: Item {}
        onClicked: root.expanded = true

        contentItem: RowLayout {
            spacing: 5
            Icon {
                source: FluentIcons.graph_SpeedHigh
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                color: currentValue.color
            }
            Label {
                text: root.fpsMonitor ? root.fpsMonitor.currentFPS.toFixed(1) + " FPS" : "-- FPS"
                font: Typography.caption
                color: root.SmoothUI.theme.res.textFillColorPrimary
            }
        }
    }

    ColumnLayout {
        id: details
        visible: root.expanded
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        clip: true

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                text: qsTr("Performance Monitor")
                font: Typography.bodyStrong
                color: root.SmoothUI.theme.res.textFillColorPrimary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            IconButton {
                implicitWidth: 24
                implicitHeight: 24
                icon.name: FluentIcons.graph_ChromeClose
                icon.width: 12
                icon.height: 12
                onClicked: root.expanded = false
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            implicitWidth: metrics.implicitWidth
            implicitHeight: metrics.implicitHeight
            contentWidth: Math.max(availableWidth, metrics.implicitWidth)
            clip: true

            ColumnLayout {
                id: metrics
                width: scroll.contentWidth
                spacing: 12

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    rowSpacing: 6
                    columnSpacing: 12

                    Label {
                        text: qsTr("Current FPS:")
                        font: Typography.caption
                        color: root.SmoothUI.theme.res.textFillColorSecondary
                        Layout.fillWidth: true
                    }
                    Label {
                        id: currentValue
                        text: root.fpsMonitor ? root.fpsMonitor.currentFPS.toFixed(1) : "--"
                        font: Typography.bodyStrong
                        Layout.alignment: Qt.AlignRight
                        color: {
                            if (!root.fpsMonitor) return root.SmoothUI.theme.res.textFillColorSecondary
                            if (root.fpsMonitor.currentFPS < 30) return root.SmoothUI.theme.res.systemFillColorCritical
                            if (root.fpsMonitor.currentFPS < 50) return root.SmoothUI.theme.res.systemFillColorCaution
                            return root.SmoothUI.theme.res.systemFillColorSuccess
                        }
                    }
                    Label { text: "FPS"; font: Typography.caption; color: root.SmoothUI.theme.res.textFillColorSecondary }

                    Label {
                        text: qsTr("Average FPS:")
                        font: Typography.caption
                        color: root.SmoothUI.theme.res.textFillColorSecondary
                        Layout.fillWidth: true
                    }
                    Label {
                        text: root.fpsMonitor ? root.fpsMonitor.averageFPS.toFixed(1) : "--"
                        font: Typography.body
                        color: root.SmoothUI.theme.res.textFillColorPrimary
                        Layout.alignment: Qt.AlignRight
                    }
                    Label { text: "FPS"; font: Typography.caption; color: root.SmoothUI.theme.res.textFillColorSecondary }

                    Label {
                        text: qsTr("Frame Time:")
                        font: Typography.caption
                        color: root.SmoothUI.theme.res.textFillColorSecondary
                        Layout.fillWidth: true
                    }
                    Label {
                        text: root.fpsMonitor ? root.fpsMonitor.frameTime.toFixed(2) : "--"
                        font: Typography.body
                        color: root.SmoothUI.theme.res.textFillColorPrimary
                        Layout.alignment: Qt.AlignRight
                    }
                    Label { text: "ms"; font: Typography.caption; color: root.SmoothUI.theme.res.textFillColorSecondary }

                    Label {
                        text: qsTr("Dropped:")
                        font: Typography.caption
                        color: root.SmoothUI.theme.res.textFillColorSecondary
                        Layout.fillWidth: true
                    }
                    Label {
                        text: root.fpsMonitor ? root.fpsMonitor.droppedFrames.toString() : "0"
                        font: Typography.body
                        Layout.alignment: Qt.AlignRight
                        color: !root.fpsMonitor || root.fpsMonitor.droppedFrames === 0
                               ? root.SmoothUI.theme.res.textFillColorPrimary
                               : root.SmoothUI.theme.res.systemFillColorCaution
                    }
                    Item {}

                    Label {
                        text: qsTr("Target:")
                        font: Typography.caption
                        color: root.SmoothUI.theme.res.textFillColorSecondary
                        Layout.fillWidth: true
                    }
                    Label {
                        text: root.targetRefreshRate.toString()
                        font: Typography.body
                        color: root.SmoothUI.theme.res.textFillColorPrimary
                        Layout.alignment: Qt.AlignRight
                    }
                    Label { text: "Hz"; font: Typography.caption; color: root.SmoothUI.theme.res.textFillColorSecondary }
                }

                Button {
                    text: qsTr("Reset")
                    Layout.alignment: Qt.AlignRight
                    enabled: !!root.fpsMonitor && typeof root.fpsMonitor.reset === "function"
                    onClicked: {
                        if (root.fpsMonitor && typeof root.fpsMonitor.reset === "function")
                            root.fpsMonitor.reset()
                    }
                }
            }
        }
    }
}
